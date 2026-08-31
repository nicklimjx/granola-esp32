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
    if (message?.type !== "board.ready" || message.protocolVersion !== 2 ||
        !message.boardId || !Array.isArray(message.supportedActions) ||
        message.supportedActions.length === 0 ||
        message.supportedActions.some(action => actionCode(action) === 0)) {
      throw new Error("invalid board.ready");
    }
    this.board = { boardId: message.boardId, supportedActions: [...message.supportedActions] };
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
    this.#newRun();
  }

  start() {
    if (!this.board || this.started || this.status === "stopped" || this.status === "complete") return [];
    this.started = true;
    this.#newRun();
    return this.#nextInstruction();
  }

  stop() {
    if (!this.board || !this.started) return [];
    const session = this.runId;
    this.started = false;
    this.active = null;
    this.status = "stopped";
    return [{ type: "send", message: { type: "game.stop", session, reset: false } }];
  }

  restart() {
    if (!this.board || (this.status !== "stopped" && this.status !== "complete")) return [];
    const oldSession = this.runId;
    this.started = true;
    this.step = 0;
    this.score = 0;
    this.active = null;
    this.lastVerdict = null;
    this.lastElapsedMs = undefined;
    this.#newRun();
    return [
      { type: "send", message: { type: "game.stop", session: oldSession, reset: true } },
      ...this.#nextInstruction(),
    ];
  }

  roundEnded(message) {
    if (message?.type !== "round.ended" || !this.active ||
        message.session !== this.active.session || message.round !== this.active.round ||
        !Number.isInteger(message.actionCode) || !Number.isInteger(message.elapsedMs)) return [];

    let result;
    if (message.actionCode === 0 || message.elapsedMs > this.active.timeoutMs) result = "timeout";
    else if (message.actionCode !== this.active.actionCode) result = "wrong_action";
    else result = "success";
    return this.#finish(result, message.elapsedMs);
  }

  watchdog(session, round) {
    if (!this.active || this.active.session !== session || this.active.round !== round) return [];
    return this.#finish("timeout");
  }

  advance(session, round) {
    if (this.active || !this.started || this.runId !== session || this.step !== round ||
        this.step >= TOTAL_STEPS) return [];
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

  #newRun() {
    this.runId = (this.runId + 1) >>> 0;
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
    const action = actions[index];
    const instruction = {
      type: "instruction",
      session: this.runId,
      round: this.step,
      action,
      actionCode: actionCode(action),
      timeoutMs: ACTION_WINDOWS_MS[Math.floor((this.step - 1) / STEPS_PER_TIER)],
    };
    this.active = instruction;
    this.status = "playing";
    return [
      { type: "send", message: instruction },
      { type: "schedule", event: "watchdog", session: instruction.session,
        round: instruction.round, delayMs: instruction.timeoutMs + WATCHDOG_GRACE_MS },
    ];
  }

  #finish(result, elapsedMs) {
    const { session, round } = this.active;
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
      { type: "audio.stop" },
      ...(this.step < TOTAL_STEPS
        ? [{ type: "schedule", event: "advance", session, round, delayMs: FEEDBACK_HOLD_MS }]
        : []),
    ];
  }
}

export function actionCode(action) {
  return ({ tap: 1, twist: 2, swipe: 3, press: 4 })[action] ?? 0;
}
