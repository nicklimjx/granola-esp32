package main

import (
	_ "embed"
	"encoding/json"
	"log"
	"math/rand"
	"net/http"
	"sort"
	"strconv"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

//go:embed dashboard.html
var dashboardHTML []byte

const (
	roundTimeout   = 2 * time.Second
	networkGrace   = 250 * time.Millisecond
	readyTimeout   = 5 * time.Second
	maxMessageSize = 4 << 10
	stepsPerRound  = 10
)

var difficultyTimeouts = [...]time.Duration{
	2 * time.Second,
	1700 * time.Millisecond,
	1400 * time.Millisecond,
	1200 * time.Millisecond,
	1 * time.Second,
	800 * time.Millisecond,
}

type Server struct {
	mu         sync.Mutex
	boards     map[string]*session
	roster     map[string]bool
	scores     map[string]int
	dashboards map[*dashboardClient]bool
	started    bool
	upgrader   websocket.Upgrader
}

type round struct {
	id      string
	action  Action
	timeout time.Duration
	timer   *time.Timer
}

type session struct {
	server  *Server
	conn    *websocket.Conn
	board   string
	actions []Action
	send    chan any
	done    chan struct{}
	stop    sync.Once

	mu     sync.Mutex
	active *round
	latest *Instruction
	step   int
	score  int
}

type dashboardClient struct {
	conn *websocket.Conn
	send chan DisplayEvent
	done chan struct{}
	stop sync.Once
}

func newServer() *Server {
	return &Server{
		boards:     make(map[string]*session),
		roster:     make(map[string]bool),
		scores:     make(map[string]int),
		dashboards: make(map[*dashboardClient]bool),
	}
}

func (s *Server) routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("/", s.dashboardPage)
	mux.HandleFunc("/state", s.state)
	mux.HandleFunc("/start", s.start)
	mux.HandleFunc("/board", s.board)
	mux.HandleFunc("/dashboard", s.dashboard)
	return mux
}

func (s *Server) dashboardPage(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	if _, err := w.Write(dashboardHTML); err != nil {
		log.Printf("write dashboard page: %v", err)
	}
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

	if !s.startGame() {
		http.Error(w, "no boards connected", http.StatusConflict)
		return
	}
	s.writeState(w)
}

func (s *Server) startGame() bool {
	s.mu.Lock()
	if !s.started && len(s.boards) == 0 {
		s.mu.Unlock()
		return false
	}
	var sessions []*session
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
	s.broadcastState()
	return true
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

func (s *Server) dashboard(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}
	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		return
	}
	conn.SetReadLimit(maxMessageSize)
	client := &dashboardClient{conn: conn, send: make(chan DisplayEvent, 1), done: make(chan struct{})}

	s.mu.Lock()
	s.dashboards[client] = true
	s.mu.Unlock()
	defer func() {
		s.mu.Lock()
		delete(s.dashboards, client)
		s.mu.Unlock()
		client.close()
	}()

	go client.writeLoop()
	client.enqueue(s.displayState())
	for {
		var command DashboardCommand
		if err := conn.ReadJSON(&command); err != nil {
			return
		}
		if command.Type != "game.start" {
			return
		}
		s.startGame()
	}
}

func (s *Server) broadcastState() {
	state := s.displayState()
	s.mu.Lock()
	defer s.mu.Unlock()
	for client := range s.dashboards {
		client.enqueue(state)
	}
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
		server:  s,
		conn:    conn,
		board:   ready.BoardID,
		actions: append([]Action(nil), ready.SupportedActions...),
		send:    make(chan any, 4),
		done:    make(chan struct{}),
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
	if ready.Type != "board.ready" || ready.ProtocolVersion != ProtocolVersion || ready.BoardID == "" || len(ready.SupportedActions) == 0 {
		return false
	}
	for _, action := range ready.SupportedActions {
		if action == "" {
			return false
		}
	}
	return true
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
	s.broadcastState()
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
	s.broadcastState()
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
	s.mu.Lock()
	if s.step == len(difficultyTimeouts)*stepsPerRound {
		s.mu.Unlock()
		return
	}
	s.step++
	timeout := difficultyTimeouts[(s.step-1)/stepsPerRound]
	action := s.actions[rand.Intn(len(s.actions))]
	r := &round{id: strconv.Itoa(s.step), action: action, timeout: timeout}
	instruction := Instruction{
		Type:      "instruction",
		RoundID:   r.id,
		Action:    r.action,
		TimeoutMS: int(timeout / time.Millisecond),
	}
	s.active = r
	s.latest = &instruction
	s.mu.Unlock()

	s.enqueue(instruction)
	if s.server != nil {
		s.server.broadcastState()
	}
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
	if *action.ElapsedMS > int(r.timeout/time.Millisecond) {
		result = ResultTimeout
	} else if action.Action != r.action {
		result = ResultWrongAction
	} else {
		s.score++
	}
	score := s.score
	s.mu.Unlock()

	s.enqueue(RoundResult{Type: "round.result", RoundID: r.id, Result: result, Score: score})
	s.startRound()
	if s.server != nil {
		s.server.broadcastState()
	}
}

func (s *session) armWatchdog(roundID string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.active == nil || s.active.id != roundID || s.active.timer != nil {
		return
	}
	s.active.timer = time.AfterFunc(s.active.timeout+networkGrace, func() {
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
	s.startRound()
	if s.server != nil {
		s.server.broadcastState()
	}
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

func (c *dashboardClient) enqueue(state DisplayEvent) {
	select {
	case c.send <- state:
		return
	default:
	}
	select {
	case <-c.send:
	default:
	}
	select {
	case c.send <- state:
	case <-c.done:
	}
}

func (c *dashboardClient) writeLoop() {
	for {
		select {
		case state := <-c.send:
			if err := c.conn.WriteJSON(state); err != nil {
				c.close()
				return
			}
		case <-c.done:
			return
		}
	}
}

func (c *dashboardClient) close() {
	c.stop.Do(func() {
		close(c.done)
		c.conn.Close()
	})
}

func main() {
	server := newServer()
	log.Println("WebSocket server listening on :8080")
	log.Fatal(http.ListenAndServe(":8080", server.routes()))
}
