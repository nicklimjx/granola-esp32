package main

import (
	"encoding/json"
	"errors"
	"net"
	"net/http"
	"net/http/httptest"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/gorilla/websocket"
)

func testServer(t *testing.T) (*Server, *httptest.Server) {
	t.Helper()
	server := newServer()
	httpServer := httptest.NewServer(server.routes())
	t.Cleanup(httpServer.Close)
	return server, httpServer
}

func connectBoard(t *testing.T, httpServer *httptest.Server, boardID string) *websocket.Conn {
	t.Helper()
	return connectBoardWithActions(t, httpServer, boardID, []Action{ActionTap})
}

func connectBoardWithActions(t *testing.T, httpServer *httptest.Server, boardID string, actions []Action) *websocket.Conn {
	t.Helper()
	url := "ws" + strings.TrimPrefix(httpServer.URL, "http") + "/board"
	conn, _, err := websocket.DefaultDialer.Dial(url, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { conn.Close() })
	if err := conn.WriteJSON(BoardReady{
		Type:             "board.ready",
		ProtocolVersion:  ProtocolVersion,
		BoardID:          boardID,
		SupportedActions: actions,
	}); err != nil {
		t.Fatal(err)
	}
	return conn
}

func connectDashboard(t *testing.T, httpServer *httptest.Server) *websocket.Conn {
	t.Helper()
	url := "ws" + strings.TrimPrefix(httpServer.URL, "http") + "/dashboard"
	conn, _, err := websocket.DefaultDialer.Dial(url, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { conn.Close() })
	return conn
}

func readDashboardUntil(t *testing.T, conn *websocket.Conn, matches func(DisplayEvent) bool) DisplayEvent {
	t.Helper()
	conn.SetReadDeadline(time.Now().Add(time.Second))
	for {
		var state DisplayEvent
		if err := conn.ReadJSON(&state); err != nil {
			t.Fatal(err)
		}
		if matches(state) {
			return state
		}
	}
}

func startGame(t *testing.T, httpServer *httptest.Server) DisplayEvent {
	t.Helper()
	response, err := http.Post(httpServer.URL+"/start", "application/json", nil)
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		t.Fatalf("start status = %d, want %d", response.StatusCode, http.StatusOK)
	}
	var state DisplayEvent
	if err := json.NewDecoder(response.Body).Decode(&state); err != nil {
		t.Fatal(err)
	}
	return state
}

func readInstruction(t *testing.T, conn *websocket.Conn) Instruction {
	t.Helper()
	var instruction Instruction
	if err := conn.ReadJSON(&instruction); err != nil {
		t.Fatal(err)
	}
	return instruction
}

func connectStartedBoard(t *testing.T, httpServer *httptest.Server, boardID string) (*websocket.Conn, Instruction) {
	t.Helper()
	conn := connectBoard(t, httpServer, boardID)
	startGame(t, httpServer)
	return conn, readInstruction(t, conn)
}

func getState(t *testing.T, httpServer *httptest.Server) DisplayEvent {
	t.Helper()
	response, err := http.Get(httpServer.URL + "/state")
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	if response.StatusCode != http.StatusOK {
		t.Fatalf("state status = %d, want %d", response.StatusCode, http.StatusOK)
	}
	var state DisplayEvent
	if err := json.NewDecoder(response.Body).Decode(&state); err != nil {
		t.Fatal(err)
	}
	return state
}

func intPointer(value int) *int {
	return &value
}

func TestBoardReceivesNoInstructionBeforeStart(t *testing.T) {
	_, httpServer := testServer(t)
	conn := connectBoard(t, httpServer, "waiting-board")
	conn.SetReadDeadline(time.Now().Add(100 * time.Millisecond))
	if _, _, err := conn.ReadMessage(); err == nil {
		t.Fatal("board received a message before game start")
	} else if netErr, ok := err.(net.Error); !ok || !netErr.Timeout() {
		t.Fatalf("read before start = %v, want timeout", err)
	}
}

func TestLobbyReportsConnectedBoardWaiting(t *testing.T) {
	_, httpServer := testServer(t)
	connectBoard(t, httpServer, "lobby-board")

	state := getState(t, httpServer)
	if state.Started || len(state.Boards) != 1 {
		t.Fatalf("unexpected lobby state: %+v", state)
	}
	if board := state.Boards[0]; board.BoardID != "lobby-board" || board.Status != StatusWaiting || board.Instruction != nil || board.Score != 0 {
		t.Fatalf("unexpected lobby board: %+v", board)
	}
}

func TestStartRejectsEmptyRosterWithoutLocking(t *testing.T) {
	_, httpServer := testServer(t)

	response, err := http.Post(httpServer.URL+"/start", "application/json", nil)
	if err != nil {
		t.Fatal(err)
	}
	response.Body.Close()
	if response.StatusCode != http.StatusConflict {
		t.Fatalf("empty start status = %d, want %d", response.StatusCode, http.StatusConflict)
	}
	if state := getState(t, httpServer); state.Started {
		t.Fatalf("empty start locked server: %+v", state)
	}

	conn := connectBoard(t, httpServer, "later-board")
	deadline := time.Now().Add(time.Second)
	for len(getState(t, httpServer).Boards) == 0 {
		if time.Now().After(deadline) {
			t.Fatal("board did not enter lobby")
		}
		time.Sleep(10 * time.Millisecond)
	}
	startGame(t, httpServer)
	readInstruction(t, conn)
}

func TestStartLocksRosterAndExcludesLateBoard(t *testing.T) {
	_, httpServer := testServer(t)
	first := connectBoard(t, httpServer, "first")
	second := connectBoard(t, httpServer, "second")
	startGame(t, httpServer)
	firstInstruction := readInstruction(t, first)
	secondInstruction := readInstruction(t, second)
	if firstInstruction != secondInstruction {
		t.Fatalf("locked boards received different instructions: first=%+v second=%+v", firstInstruction, secondInstruction)
	}

	first.Close()
	deadline := time.Now().Add(time.Second)
	for {
		state := getState(t, httpServer)
		if len(state.Boards) == 2 && state.Boards[0].Status == StatusDisconnected {
			break
		}
		if time.Now().After(deadline) {
			t.Fatalf("locked disconnected board did not remain visible: %+v", state)
		}
		time.Sleep(10 * time.Millisecond)
	}

	late := connectBoard(t, httpServer, "late")
	late.SetReadDeadline(time.Now().Add(time.Second))
	if _, _, err := late.ReadMessage(); err == nil {
		t.Fatal("late board connection remained open")
	}

	state := getState(t, httpServer)
	if len(state.Boards) != 2 || state.Boards[0].BoardID != "first" || state.Boards[1].BoardID != "second" {
		t.Fatalf("late board changed locked roster: %+v", state.Boards)
	}
}

func TestDashboardSocketStartsGameAndReceivesSnapshots(t *testing.T) {
	_, httpServer := testServer(t)
	board := connectBoard(t, httpServer, "dashboard-board")
	dashboard := connectDashboard(t, httpServer)

	lobby := readDashboardUntil(t, dashboard, func(state DisplayEvent) bool {
		return !state.Started && len(state.Boards) == 1
	})
	if lobby.Boards[0].Status != StatusWaiting {
		t.Fatalf("unexpected dashboard lobby: %+v", lobby)
	}
	if err := dashboard.WriteJSON(DashboardCommand{Type: "game.start"}); err != nil {
		t.Fatal(err)
	}
	instruction := readInstruction(t, board)
	started := readDashboardUntil(t, dashboard, func(state DisplayEvent) bool {
		return state.Started && len(state.Boards) == 1 && state.Boards[0].Status == StatusPlaying
	})
	if started.Boards[0].Instruction == nil || *started.Boards[0].Instruction != instruction {
		t.Fatalf("dashboard snapshot missing instruction: state=%+v instruction=%+v", started, instruction)
	}
}

func TestDashboardReportsCurrentInstruction(t *testing.T) {
	_, httpServer := testServer(t)
	conn := connectBoard(t, httpServer, "playing-board")
	startGame(t, httpServer)
	instruction := readInstruction(t, conn)

	state := getState(t, httpServer)
	if len(state.Boards) != 1 {
		t.Fatalf("got %d boards, want 1", len(state.Boards))
	}
	board := state.Boards[0]
	if board.Status != StatusPlaying || board.Instruction == nil || *board.Instruction != instruction {
		t.Fatalf("dashboard did not report active instruction: board=%+v instruction=%+v", board, instruction)
	}
}

func TestCompletedStepDashboardShowsNextInstruction(t *testing.T) {
	_, httpServer := testServer(t)
	conn := connectBoard(t, httpServer, "fast-board")
	startGame(t, httpServer)
	instruction := readInstruction(t, conn)
	if err := conn.WriteJSON(ActionDetected{
		Type:      "action.detected",
		RoundID:   instruction.RoundID,
		Action:    instruction.Action,
		ElapsedMS: intPointer(1),
	}); err != nil {
		t.Fatal(err)
	}
	var result RoundResult
	if err := conn.ReadJSON(&result); err != nil {
		t.Fatal(err)
	}
	next := readInstruction(t, conn)

	state := getState(t, httpServer)
	if len(state.Boards) != 1 {
		t.Fatalf("got %d boards, want 1", len(state.Boards))
	}
	board := state.Boards[0]
	if board.Status != StatusPlaying || board.Instruction == nil || *board.Instruction != next {
		t.Fatalf("dashboard missing next instruction: board=%+v instruction=%+v", board, next)
	}
}

func TestGameStartsAtTwoSecondDifficulty(t *testing.T) {
	_, httpServer := testServer(t)
	_, instruction := connectStartedBoard(t, httpServer, "board-1")
	if instruction.TimeoutMS != 2000 {
		t.Fatalf("first timeout = %d, want 2000", instruction.TimeoutMS)
	}
}

func TestBoardPlaysSixtySupportedStepsAcrossSixDifficultyRounds(t *testing.T) {
	_, httpServer := testServer(t)
	conn := connectBoardWithActions(t, httpServer, "board-1", []Action{ActionTwist})
	startGame(t, httpServer)
	instruction := readInstruction(t, conn)
	conn.SetReadDeadline(time.Now().Add(time.Second))

	for step := 1; step <= 60; step++ {
		wantTimeout := int(difficultyTimeouts[(step-1)/stepsPerRound] / time.Millisecond)
		if instruction.RoundID != strconv.Itoa(step) || instruction.TimeoutMS != wantTimeout {
			t.Fatalf("step %d instruction = %+v, want round ID %d and timeout %d", step, instruction, step, wantTimeout)
		}
		if instruction.Action != ActionTwist {
			t.Fatalf("step %d requested unsupported action %q", step, instruction.Action)
		}
		if err := conn.WriteJSON(ActionDetected{
			Type:      "action.detected",
			RoundID:   instruction.RoundID,
			Action:    instruction.Action,
			ElapsedMS: intPointer(1),
		}); err != nil {
			t.Fatal(err)
		}
		var result RoundResult
		if err := conn.ReadJSON(&result); err != nil {
			t.Fatal(err)
		}
		if result.Result != ResultSuccess || result.Score != step {
			t.Fatalf("step %d result = %+v", step, result)
		}
		if step < 60 {
			instruction = readInstruction(t, conn)
		}
	}
	conn.SetReadDeadline(time.Now().Add(100 * time.Millisecond))
	if _, _, err := conn.ReadMessage(); err == nil {
		t.Fatal("received instruction after step 60")
	} else if netErr, ok := err.(net.Error); !ok || !netErr.Timeout() {
		t.Fatalf("read after game end = %v, want timeout", err)
	}
}

func TestRoundResultUsesBoardElapsedTime(t *testing.T) {
	tests := []struct {
		name      string
		action    Action
		elapsedMS int
		result    Result
		score     int
	}{
		{name: "within timeout", action: ActionTap, elapsedMS: int(roundTimeout / time.Millisecond), result: ResultSuccess, score: 1},
		{name: "past timeout", action: ActionTap, elapsedMS: int(roundTimeout/time.Millisecond) + 1, result: ResultTimeout, score: 0},
		{name: "late wrong action", action: "shake", elapsedMS: int(roundTimeout/time.Millisecond) + 1, result: ResultTimeout, score: 0},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			_, httpServer := testServer(t)
			conn, instruction := connectStartedBoard(t, httpServer, "board-1")
			if instruction.Action != ActionTap || instruction.TimeoutMS != int(roundTimeout/time.Millisecond) {
				t.Fatalf("unexpected instruction: %+v", instruction)
			}
			if err := conn.WriteJSON(ActionDetected{
				Type:      "action.detected",
				RoundID:   instruction.RoundID,
				Action:    test.action,
				ElapsedMS: intPointer(test.elapsedMS),
			}); err != nil {
				t.Fatal(err)
			}
			var result RoundResult
			if err := conn.ReadJSON(&result); err != nil {
				t.Fatal(err)
			}
			if result.Result != test.result || result.Score != test.score {
				t.Fatalf("got result=%q score=%d, want result=%q score=%d", result.Result, result.Score, test.result, test.score)
			}
		})
	}
}

func TestMissingElapsedMSClosesConnection(t *testing.T) {
	tests := []struct {
		name    string
		message string
	}{
		{name: "omitted", message: `{"type":"action.detected","roundId":"1","action":"tap"}`},
		{name: "null", message: `{"type":"action.detected","roundId":"1","action":"tap","elapsedMs":null}`},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			_, httpServer := testServer(t)
			conn, _ := connectStartedBoard(t, httpServer, "missing-elapsed")
			if err := conn.WriteMessage(websocket.TextMessage, []byte(test.message)); err != nil {
				t.Fatal(err)
			}
			conn.SetReadDeadline(time.Now().Add(time.Second))
			_, _, err := conn.ReadMessage()
			if err == nil {
				t.Fatal("connection remained open")
			}
			var netErr net.Error
			if errors.As(err, &netErr) && netErr.Timeout() {
				t.Fatal("connection was not closed before the read deadline")
			}
		})
	}
}

func TestWatchdogCompletesNoResponse(t *testing.T) {
	_, httpServer := testServer(t)
	conn, instruction := connectStartedBoard(t, httpServer, "silent-board")
	conn.SetReadDeadline(time.Now().Add(roundTimeout + networkGrace + time.Second))

	var result RoundResult
	if err := conn.ReadJSON(&result); err != nil {
		t.Fatal(err)
	}
	if result.RoundID != instruction.RoundID || result.Result != ResultTimeout || result.Score != 0 {
		t.Fatalf("unexpected watchdog result: %+v", result)
	}
}

func TestCompletedRoundIgnoresStaleAction(t *testing.T) {
	sess := &session{
		send:    make(chan any, 2),
		done:    make(chan struct{}),
		actions: []Action{ActionTap},
		active:  &round{id: "1", action: ActionTap, timeout: roundTimeout},
		step:    1,
	}
	action := ActionDetected{Type: "action.detected", RoundID: "1", Action: ActionTap, ElapsedMS: intPointer(10)}
	sess.complete(action)
	<-sess.send
	<-sess.send

	sess.complete(action)
	if got := len(sess.send); got != 0 {
		t.Fatalf("stale action produced %d additional result(s)", got)
	}
}

func TestDuplicateBoardReplacesOldSession(t *testing.T) {
	server, httpServer := testServer(t)
	old := connectBoard(t, httpServer, "same-board")
	server.mu.Lock()
	oldSession := server.boards["same-board"]
	server.mu.Unlock()

	connectBoard(t, httpServer, "same-board")
	old.SetReadDeadline(time.Now().Add(time.Second))
	if _, _, err := old.ReadMessage(); err == nil {
		t.Fatal("old connection remained open")
	}

	server.mu.Lock()
	current := server.boards["same-board"]
	server.mu.Unlock()
	if current == nil || current == oldSession {
		t.Fatal("old session cleanup removed the replacement")
	}
}
