package main

import (
	"encoding/json"
	"log"
	"net/http"
	"sort"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

const (
	roundTimeout   = 1500 * time.Millisecond
	networkGrace   = 250 * time.Millisecond
	readyTimeout   = 5 * time.Second
	maxMessageSize = 4 << 10
)

type Server struct {
	mu       sync.Mutex
	boards   map[string]*session
	roster   map[string]bool
	scores   map[string]int
	started  bool
	upgrader websocket.Upgrader
}

type round struct {
	id    string
	timer *time.Timer
}

type session struct {
	conn  *websocket.Conn
	board string
	send  chan any
	done  chan struct{}
	stop  sync.Once

	mu     sync.Mutex
	active *round
	latest *Instruction
	score  int
}

func newServer() *Server {
	return &Server{
		boards: make(map[string]*session),
		roster: make(map[string]bool),
		scores: make(map[string]int),
	}
}

func (s *Server) routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/state", s.state)
	mux.HandleFunc("/start", s.start)
	mux.HandleFunc("/board", s.board)
	return mux
}

func (s *Server) state(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	s.writeState(w)
}

func (s *Server) start(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	s.mu.Lock()
	var sessions []*session
	if !s.started && len(s.boards) == 0 {
		s.mu.Unlock()
		http.Error(w, "no boards connected", http.StatusConflict)
		return
	}
	if !s.started {
		s.started = true
		for boardID, sess := range s.boards {
			s.roster[boardID] = true
			sessions = append(sessions, sess)
		}
	}
	s.mu.Unlock()

	for _, sess := range sessions {
		sess.startRound()
	}
	s.writeState(w)
}

func (s *Server) writeState(w http.ResponseWriter) {
	w.Header().Set("Content-Type", "application/json")
	if err := json.NewEncoder(w).Encode(s.displayState()); err != nil {
		log.Printf("write dashboard state: %v", err)
	}
}

func (s *Server) displayState() DisplayEvent {
	s.mu.Lock()
	started := s.started
	boardIDs := make([]string, 0, len(s.boards)+len(s.roster))
	if started {
		for boardID := range s.roster {
			boardIDs = append(boardIDs, boardID)
		}
	} else {
		for boardID := range s.boards {
			boardIDs = append(boardIDs, boardID)
		}
	}
	sort.Strings(boardIDs)
	connected := make(map[string]*session, len(boardIDs))
	for _, boardID := range boardIDs {
		connected[boardID] = s.boards[boardID]
	}
	s.mu.Unlock()

	boards := make([]DisplayBoard, 0, len(boardIDs))
	for _, boardID := range boardIDs {
		sess := connected[boardID]
		if sess == nil {
			boards = append(boards, DisplayBoard{BoardID: boardID, Status: StatusDisconnected, Score: s.boardScore(boardID)})
			continue
		}
		boards = append(boards, sess.display())
	}
	return DisplayEvent{Type: "state", Boards: boards, Started: started}
}

func (s *Server) board(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}

	conn.SetReadLimit(maxMessageSize)
	if err := conn.SetReadDeadline(time.Now().Add(readyTimeout)); err != nil {
		conn.Close()
		return
	}

	var ready BoardReady
	if err := conn.ReadJSON(&ready); err != nil || !validReady(ready) {
		conn.Close()
		return
	}
	if err := conn.SetReadDeadline(time.Time{}); err != nil {
		conn.Close()
		return
	}

	sess := &session{
		conn:  conn,
		board: ready.BoardID,
		send:  make(chan any, 4),
		done:  make(chan struct{}),
	}
	admitted, startRound := s.register(sess)
	if !admitted {
		sess.close()
		return
	}
	defer func() {
		s.unregister(sess)
		sess.close()
	}()

	go sess.writeLoop()
	if startRound {
		sess.startRound()
	}

	for {
		var action ActionDetected
		if err := conn.ReadJSON(&action); err != nil {
			return
		}
		if action.Type != "action.detected" {
			continue
		}
		if action.ElapsedMS == nil || *action.ElapsedMS < 0 {
			return
		}
		sess.complete(action)
	}
}

func validReady(ready BoardReady) bool {
	if ready.Type != "board.ready" || ready.ProtocolVersion != ProtocolVersion || ready.BoardID == "" {
		return false
	}
	for _, action := range ready.SupportedActions {
		if action == ActionTap {
			return true
		}
	}
	return false
}

func (s *Server) register(sess *session) (admitted, startRound bool) {
	s.mu.Lock()
	if s.started && !s.roster[sess.board] {
		s.mu.Unlock()
		return false, false
	}
	old := s.boards[sess.board]
	sess.score = s.scores[sess.board]
	if old != nil {
		old.mu.Lock()
		sess.score = old.score
		old.mu.Unlock()
	}
	s.scores[sess.board] = sess.score
	s.boards[sess.board] = sess
	startRound = s.started
	s.mu.Unlock()
	if old != nil {
		old.close()
	}
	return true, startRound
}

func (s *Server) unregister(sess *session) {
	s.mu.Lock()
	if s.boards[sess.board] == sess {
		sess.mu.Lock()
		s.scores[sess.board] = sess.score
		sess.mu.Unlock()
		delete(s.boards, sess.board)
	}
	s.mu.Unlock()
}

func (s *Server) boardScore(boardID string) int {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.scores[boardID]
}

func (s *session) display() DisplayBoard {
	s.mu.Lock()
	defer s.mu.Unlock()
	board := DisplayBoard{BoardID: s.board, Score: s.score, Status: StatusWaiting}
	if s.latest != nil {
		instruction := *s.latest
		board.Instruction = &instruction
	}
	if s.active != nil {
		board.Status = StatusPlaying
	}
	return board
}

func (s *session) startRound() {
	r := &round{id: "1"}
	instruction := Instruction{
		Type:      "instruction",
		RoundID:   r.id,
		Action:    ActionTap,
		TimeoutMS: int(roundTimeout / time.Millisecond),
	}
	s.mu.Lock()
	s.active = r
	s.latest = &instruction
	s.mu.Unlock()

	s.enqueue(instruction)
}

func (s *session) complete(action ActionDetected) {
	s.mu.Lock()
	if s.active == nil || s.active.id != action.RoundID {
		s.mu.Unlock()
		return
	}
	r := s.active
	s.active = nil
	if r.timer != nil {
		r.timer.Stop()
	}

	result := ResultSuccess
	if *action.ElapsedMS > int(roundTimeout/time.Millisecond) {
		result = ResultTimeout
	} else if action.Action != ActionTap {
		result = ResultWrongAction
	} else {
		s.score = 1
	}
	score := s.score
	s.mu.Unlock()

	s.enqueue(RoundResult{Type: "round.result", RoundID: r.id, Result: result, Score: score})
}

func (s *session) armWatchdog(roundID string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.active == nil || s.active.id != roundID || s.active.timer != nil {
		return
	}
	s.active.timer = time.AfterFunc(roundTimeout+networkGrace, func() {
		s.completeTimeout(roundID)
	})
}

func (s *session) completeTimeout(roundID string) {
	s.mu.Lock()
	if s.active == nil || s.active.id != roundID {
		s.mu.Unlock()
		return
	}
	s.active = nil
	score := s.score
	s.mu.Unlock()

	s.enqueue(RoundResult{Type: "round.result", RoundID: roundID, Result: ResultTimeout, Score: score})
}

func (s *session) enqueue(message any) {
	select {
	case s.send <- message:
	case <-s.done:
	}
}

func (s *session) writeLoop() {
	for {
		select {
		case message := <-s.send:
			if err := s.conn.WriteJSON(message); err != nil {
				s.close()
				return
			}
			if instruction, ok := message.(Instruction); ok {
				s.armWatchdog(instruction.RoundID)
			}
		case <-s.done:
			return
		}
	}
}

func (s *session) close() {
	s.stop.Do(func() {
		s.mu.Lock()
		if s.active != nil && s.active.timer != nil {
			s.active.timer.Stop()
		}
		s.active = nil
		s.mu.Unlock()
		close(s.done)
		s.conn.Close()
	})
}

func main() {
	server := newServer()
	log.Println("WebSocket server listening on :8080")
	log.Fatal(http.ListenAndServe(":8080", server.routes()))
}
