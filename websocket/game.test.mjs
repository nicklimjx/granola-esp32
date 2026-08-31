import assert from "node:assert/strict";
import test from "node:test";
import {
  ACTION_WINDOWS_MS,
  FEEDBACK_HOLD_MS,
  Game,
  TOTAL_STEPS,
  WATCHDOG_GRACE_MS,
} from "./game.mjs";

const ready = {
  type: "board.ready",
  protocolVersion: 2,
  boardId: "bopit-01",
  supportedActions: ["tap", "swipe", "press"],
};

const sends = effects => effects.filter(effect => effect.type === "send").map(effect => effect.message);
const instruction = effects => sends(effects).find(message => message.type === "instruction");
const schedules = effects => effects.filter(effect => effect.type === "schedule");
const hasAudioStop = effects => effects.some(effect => effect.type === "audio.stop");

function endRound(game, current, actionCode = current.actionCode, elapsedMs = 125) {
  return game.roundEnded({
    type: "round.ended",
    session: current.session,
    round: current.round,
    actionCode,
    elapsedMs,
  });
}

function assertNoResultWrite(effects) {
  assert.equal(sends(effects).some(message => message.type === "round.result"), false);
}

test("plays exactly 60 rounds across timeout tiers with no result writes", () => {
  assert.deepEqual(ACTION_WINDOWS_MS, [4000, 3400, 2800, 2400, 2000, 1600]);
  const game = new Game(() => 0.5);
  game.acceptReady(ready);
  let effects = game.start();
  const session = instruction(effects).session;

  for (let round = 1; round <= TOTAL_STEPS; round += 1) {
    const current = instruction(effects);
    assert.deepEqual(current, {
      type: "instruction",
      session,
      round,
      action: "swipe",
      actionCode: 3,
      timeoutMs: ACTION_WINDOWS_MS[Math.floor((round - 1) / 10)],
    });
    assert.deepEqual(schedules(effects), [{
      type: "schedule",
      event: "watchdog",
      session,
      round,
      delayMs: current.timeoutMs + WATCHDOG_GRACE_MS,
    }]);

    effects = endRound(game, current, current.actionCode, current.timeoutMs);
    assertNoResultWrite(effects);
    assert.equal(hasAudioStop(effects), true);
    assert.equal(game.score, round);
    assert.equal(game.snapshot().boards[0].lastVerdict, "success");
    assert.equal(game.snapshot().boards[0].elapsedMs, current.timeoutMs);

    if (round < TOTAL_STEPS) {
      assert.deepEqual(schedules(effects), [{
        type: "schedule", event: "advance", session, round, delayMs: FEEDBACK_HOLD_MS,
      }]);
      effects = game.advance(session, round);
    } else {
      assert.deepEqual(effects, [{ type: "audio.stop" }]);
      assert.equal(game.status, "complete");
      assert.equal(game.started, false);
      assert.deepEqual(game.advance(session, round), []);
    }
  }
});

test("derives success, wrong, timeout, and exact-deadline success from round events", () => {
  const game = new Game(() => 0);
  game.acceptReady({ ...ready, supportedActions: ["tap"] });

  let current = instruction(game.start());
  let effects = endRound(game, current, 4, 20);
  assert.equal(game.score, 0);
  assert.equal(game.lastVerdict, "wrong_action");
  assert.equal(hasAudioStop(effects), true);

  current = instruction(game.advance(current.session, current.round));
  effects = endRound(game, current, 1, current.timeoutMs);
  assert.equal(game.score, 1, "elapsed == timeout must count");
  assert.equal(game.lastVerdict, "success");

  current = instruction(game.advance(current.session, current.round));
  effects = endRound(game, current, 1, current.timeoutMs + 1);
  assert.equal(game.score, 1);
  assert.equal(game.lastVerdict, "timeout");

  current = instruction(game.advance(current.session, current.round));
  effects = endRound(game, current, 0, current.timeoutMs);
  assert.equal(game.lastVerdict, "timeout");
  assertNoResultWrite(effects);
});

test("watchdog is recovery, stops audio, and duplicate or stale completions are ignored", () => {
  const game = new Game(() => 0);
  game.acceptReady(ready);
  const current = instruction(game.start());

  const staleSession = game.roundEnded({
    type: "round.ended", session: current.session + 1, round: current.round,
    actionCode: current.actionCode, elapsedMs: 10,
  });
  const staleRound = game.roundEnded({
    type: "round.ended", session: current.session, round: current.round + 1,
    actionCode: current.actionCode, elapsedMs: 10,
  });
  assert.deepEqual(staleSession, []);
  assert.deepEqual(staleRound, []);
  assert.equal(game.active, current);

  const effects = game.watchdog(current.session, current.round);
  assert.equal(hasAudioStop(effects), true);
  assert.equal(game.lastVerdict, "timeout");
  assert.equal(game.lastElapsedMs, undefined);
  assertNoResultWrite(effects);
  assert.deepEqual(game.watchdog(current.session, current.round), []);
  assert.deepEqual(endRound(game, current), []);
});

test("stop and restart preserve then reset score and order old-session stop first", () => {
  const game = new Game(() => 0);
  game.acceptReady(ready);
  const first = instruction(game.start());
  endRound(game, first, first.actionCode, 90);
  assert.equal(game.score, 1);

  assert.deepEqual(game.stop(), [{
    type: "send", message: { type: "game.stop", session: first.session, reset: false },
  }]);
  assert.equal(game.status, "stopped");
  assert.equal(game.score, 1);

  const effects = game.restart();
  const messages = sends(effects);
  assert.deepEqual(messages[0], { type: "game.stop", session: first.session, reset: true });
  assert.equal(messages[1].type, "instruction");
  assert.equal(messages[1].session, (first.session + 1) >>> 0);
  assert.equal(messages[1].round, 1);
  assert.equal(game.score, 0);
  assert.equal(game.lastVerdict, null);
  assert.deepEqual(game.watchdog(first.session, first.round), []);
  assert.equal(game.active.session, messages[1].session);
});

test("completion restart, disconnect, and readiness validation isolate runs", () => {
  const game = new Game(() => 0);
  assert.throws(() => game.acceptReady({ ...ready, protocolVersion: 1 }), /invalid board.ready/);
  assert.throws(() => game.acceptReady({ ...ready, supportedActions: [] }), /invalid board.ready/);
  assert.throws(() => game.acceptReady({ ...ready, supportedActions: ["twist-left"] }), /invalid board.ready/);
  game.acceptReady({ ...ready, supportedActions: ["press"] });

  let effects = game.start();
  let final;
  for (let round = 1; round <= TOTAL_STEPS; round += 1) {
    final = instruction(effects);
    effects = endRound(game, final, 4, 0);
    if (round < TOTAL_STEPS) effects = game.advance(final.session, final.round);
  }
  assert.equal(game.status, "complete");
  assert.deepEqual(effects, [{ type: "audio.stop" }]);

  const restarted = game.restart();
  const fresh = instruction(restarted);
  assert.equal(fresh.round, 1);
  assert.notEqual(fresh.session, final.session);
  assert.deepEqual(game.roundEnded({
    type: "round.ended", session: final.session, round: final.round, actionCode: 4, elapsedMs: 1,
  }), []);
  assert.equal(game.active.session, fresh.session);

  game.disconnect();
  assert.deepEqual(game.snapshot(), { boards: [], started: false });
  assert.deepEqual(game.watchdog(fresh.session, fresh.round), []);
  game.acceptReady({ ...ready, supportedActions: ["tap"] });
  const reconnected = instruction(game.start());
  assert.equal(reconnected.round, 1);
  assert.notEqual(reconnected.session, fresh.session);
});
