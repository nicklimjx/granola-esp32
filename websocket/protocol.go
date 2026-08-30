package main

const ProtocolVersion = 1

type Action string

type Result string

const (
	ActionTap   Action = "tap"
	ActionTwist Action = "twist"
	ActionSwipe Action = "swipe"
	ActionPress Action = "press"

	ResultSuccess     Result = "success"
	ResultWrongAction Result = "wrong_action"
	ResultTimeout     Result = "timeout"
)

type DashboardCommand struct {
	Type string `json:"type"`
}

type BoardReady struct {
	Type             string   `json:"type"`
	ProtocolVersion  int      `json:"protocolVersion"`
	BoardID          string   `json:"boardId"`
	SupportedActions []Action `json:"supportedActions"`
}

type Instruction struct {
	Type      string `json:"type"`
	RoundID   string `json:"roundId"`
	Action    Action `json:"action"`
	TimeoutMS int    `json:"timeoutMs"`
}

type ActionDetected struct {
	Type      string `json:"type"`
	RoundID   string `json:"roundId"`
	Action    Action `json:"action"`
	ElapsedMS *int   `json:"elapsedMs"`
}

type RoundResult struct {
	Type    string `json:"type"`
	RoundID string `json:"roundId"`
	Result  Result `json:"result"`
	Score   int    `json:"score"`
}

type BoardStatus string

const (
	StatusWaiting      BoardStatus = "waiting"
	StatusPlaying      BoardStatus = "playing"
	StatusDisconnected BoardStatus = "disconnected"
)

type DisplayBoard struct {
	BoardID     string       `json:"boardId"`
	Status      BoardStatus  `json:"status"`
	Score       int          `json:"score"`
	Instruction *Instruction `json:"instruction,omitempty"`
}

type DisplayEvent struct {
	Type        string         `json:"type"`
	BoardID     string         `json:"boardId,omitempty"`
	Board       *DisplayBoard  `json:"board,omitempty"`
	Boards      []DisplayBoard `json:"boards"`
	Instruction *Instruction   `json:"instruction,omitempty"`
	RoundResult *RoundResult   `json:"roundResult,omitempty"`
	Started     bool           `json:"started"`
}
