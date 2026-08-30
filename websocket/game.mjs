export const ACTION_WINDOWS_MS = Object.freeze([2000, 1700, 1400, 1200, 1000, 800]);
export const STEPS_PER_TIER = 10;
export const TOTAL_STEPS = ACTION_WINDOWS_MS.length * STEPS_PER_TIER;
export const WATCHDOG_GRACE_MS = 250;
export const FEEDBACK_HOLD_MS = 350;

export class Game {
  constructor(random = Math.random) {
    this.random = random;
    this.board = null;
    this.started = false;
    this.step = 0;
    this.score = 0;
    this.active = null;
    this.status = "disconnected";
  }

  acceptReady(message) {
    if (message?.type !== "board.ready" || message.protocolVersion !== 1 ||
        !message.boardId || !Array.isArray(message.supportedActions) ||
        message.supportedActions.length === 0 || message.supportedActions.some(action => !action)) {
      throw new Error("invalid board.ready");
    }
    this.board = {
      boardId: message.boardId,
      supportedActions: [...message.supportedActions],
    };
    if (!this.started) this.status = "waiting";
    return [];
  }

  disconnect() {
    this.board = null;
    this.started = false;
    this.step = 0;
    this.score = 0;
    this.active = null;
    this.status = "disconnected";
  }

  start() {
    if (!this.board || this.started) return [];
    this.started = true;
    return this.#nextInstruction();
  }

  actionDetected(message) {
    if (message?.type !== "action.detected" || !this.active ||
        message.roundId !== this.active.roundId || !Number.isFinite(message.elapsedMs) ||
        message.elapsedMs < 0) return [];

    let result = "success";
    if (message.elapsedMs > this.active.timeoutMs) result = "timeout";
    else if (message.action !== this.active.action) result = "wrong_action";
    return this.#finish(result);
  }

  watchdog(roundId) {
    if (!this.active || this.active.roundId !== roundId) return [];
    return this.#finish("timeout");
  }

  advance(roundId) {
    if (this.active || String(this.step) !== roundId || this.step >= TOTAL_STEPS) return [];
    return this.#nextInstruction();
  }

  snapshot() {
    if (!this.board) return { boards: [], started: this.started };
    return {
      started: this.started,
      boards: [{
        boardId: this.board.boardId,
        status: this.status,
        score: this.score,
        instruction: this.active ? { ...this.active } : undefined,
      }],
    };
  }

  #nextInstruction() {
    if (this.step >= TOTAL_STEPS) {
      this.status = "complete";
      return [];
    }
    this.step += 1;
    const actions = this.board.supportedActions;
    const index = Math.min(actions.length - 1, Math.floor(this.random() * actions.length));
    const instruction = {
      type: "instruction",
      roundId: String(this.step),
      action: actions[index],
      timeoutMs: ACTION_WINDOWS_MS[Math.floor((this.step - 1) / STEPS_PER_TIER)],
    };
    this.active = instruction;
    this.status = "playing";
    return [
      { type: "send", message: instruction },
      { type: "schedule", event: "watchdog", roundId: instruction.roundId,
        delayMs: instruction.timeoutMs + WATCHDOG_GRACE_MS },
    ];
  }

  #finish(result) {
    const roundId = this.active.roundId;
    this.active = null;
    if (result === "success") this.score += 1;
    this.status = this.step === TOTAL_STEPS ? "complete" : "feedback";
    return [
      { type: "send", message: { type: "round.result", roundId, result, score: this.score } },
      ...(this.step < TOTAL_STEPS
        ? [{ type: "schedule", event: "advance", roundId, delayMs: FEEDBACK_HOLD_MS }]
        : []),
    ];
  }
}
