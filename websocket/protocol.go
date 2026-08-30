package websocket

const ProtocolVersion = 1

type Action string

type Result string

const (
	ResultSuccess     Result = "success"
	ResultWrongAction Result = "wrong_action"
	ResultTimeout     Result = "timeout"
)

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
	ElapsedMS int    `json:"elapsedMs"`
}

type RoundResult struct {
	Type    string `json:"type"`
	RoundID string `json:"roundId"`
	Result  Result `json:"result"`
	Score   int    `json:"score"`
}
