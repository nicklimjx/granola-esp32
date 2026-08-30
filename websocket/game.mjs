export const ACTION_WINDOWS_MS = Object.freeze([4000, 3400, 2800, 2400, 2000, 1600]);
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
    this.lastVerdict = null;
    this.lastElapsedMs = undefined;
    this.runId = 0;
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
    this.lastVerdict = null;
    this.lastElapsedMs = undefined;
    this.runId += 1;
  }

  start() {
    if (!this.board || this.started || this.status === "stopped" || this.status === "complete") return [];
    this.started = true;
    this.runId += 1;
    return this.#nextInstruction();
  }

  stop() {
    if (!this.board || !this.started) return [];
    this.started = false;
    this.active = null;
    this.status = "stopped";
    return [{ type: "send", message: { type: "game.stop", reset: false } }];
  }

  restart() {
    if (!this.board || (this.status !== "stopped" && this.status !== "complete")) return [];
    this.started = true;
    this.step = 0;
    this.score = 0;
    this.active = null;
    this.lastVerdict = null;
    this.lastElapsedMs = undefined;
    this.runId += 1;
    return [
      { type: "send", message: { type: "game.stop", reset: true } },
      ...this.#nextInstruction(),
    ];
  }

  actionDetected(message) {
    if (message?.type !== "action.detected" || !this.active ||
        message.roundId !== this.active.roundId || !Number.isFinite(message.elapsedMs) ||
        message.elapsedMs < 0) return [];

    let result = "success";
    if (message.elapsedMs > this.active.timeoutMs) result = "timeout";
    else if (message.action !== this.active.action) result = "wrong_action";
    return this.#finish(result, message.elapsedMs);
  }

  watchdog(roundId) {
    if (!this.active || this.active.roundId !== roundId) return [];
    return this.#finish("timeout");
  }

  advance(roundId) {
    if (this.active || !this.started || `${this.runId}:${this.step}` !== roundId || this.step >= TOTAL_STEPS) return [];
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
        lastVerdict: this.lastVerdict ?? undefined,
        elapsedMs: this.lastElapsedMs,
        instruction: this.active ? { ...this.active } : undefined,
      }],
    };
  }

  #nextInstruction() {
    if (this.step >= TOTAL_STEPS) {
      this.started = false;
      this.status = "complete";
      return [];
    }
    this.step += 1;
    const actions = this.board.supportedActions;
    const index = Math.min(actions.length - 1, Math.floor(this.random() * actions.length));
    const instruction = {
      type: "instruction",
      roundId: `${this.runId}:${this.step}`,
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

  #finish(result, elapsedMs) {
    const roundId = this.active.roundId;
    this.active = null;
    this.lastVerdict = result;
    this.lastElapsedMs = elapsedMs;
    if (result === "success") this.score += 1;
    if (this.step === TOTAL_STEPS) {
      this.started = false;
      this.status = "complete";
    } else {
      this.status = "feedback";
    }
    return [
      { type: "send", message: { type: "round.result", roundId, result, score: this.score } },
      ...(this.step < TOTAL_STEPS
        ? [{ type: "schedule", event: "advance", roundId, delayMs: FEEDBACK_HOLD_MS }]
        : []),
    ];
  }
}
