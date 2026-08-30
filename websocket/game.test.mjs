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
  protocolVersion: 1,
  boardId: "bopit-01",
  supportedActions: ["tap", "twist", "swipe", "press"],
};

function sendEffect(effects) {
  return effects.find(effect => effect.type === "send").message;
}

test("plays exactly 60 deterministic steps across all timeout tiers", () => {
  const game = new Game(() => 0.5);
  game.acceptReady(ready);
  let effects = game.start();

  for (let step = 1; step <= TOTAL_STEPS; step += 1) {
    const instruction = sendEffect(effects);
    assert.deepEqual(instruction, {
      type: "instruction",
      roundId: String(step),
      action: "swipe",
      timeoutMs: ACTION_WINDOWS_MS[Math.floor((step - 1) / 10)],
    });
    assert.deepEqual(effects[1], {
      type: "schedule",
      event: "watchdog",
      roundId: String(step),
      delayMs: instruction.timeoutMs + WATCHDOG_GRACE_MS,
    });

    effects = game.actionDetected({
      type: "action.detected",
      roundId: String(step),
      action: "swipe",
      elapsedMs: instruction.timeoutMs,
    });
    assert.deepEqual(sendEffect(effects), {
      type: "round.result",
      roundId: String(step),
      result: "success",
      score: step,
    });

    if (step < TOTAL_STEPS) {
      assert.deepEqual(effects[1], {
        type: "schedule",
        event: "advance",
        roundId: String(step),
        delayMs: FEEDBACK_HOLD_MS,
      });
      effects = game.advance(String(step));
    } else {
      assert.equal(effects.length, 1);
      assert.equal(game.status, "complete");
      assert.deepEqual(game.advance(String(step)), []);
    }
  }
});

test("wrong, late, watchdog, and stale actions have exact outcomes", () => {
  const game = new Game(() => 0);
  game.acceptReady({ ...ready, supportedActions: ["tap"] });
  const first = sendEffect(game.start());

  let effects = game.actionDetected({ type: "action.detected", roundId: "1", action: "twist", elapsedMs: 20 });
  assert.equal(sendEffect(effects).result, "wrong_action");
  assert.equal(game.score, 0);
  assert.deepEqual(game.actionDetected({ type: "action.detected", roundId: "1", action: "tap", elapsedMs: 20 }), []);

  const second = sendEffect(game.advance("1"));
  effects = game.actionDetected({ type: "action.detected", roundId: "2", action: "tap", elapsedMs: second.timeoutMs + 1 });
  assert.equal(sendEffect(effects).result, "timeout");

  game.advance("2");
  effects = game.watchdog("3");
  assert.equal(sendEffect(effects).result, "timeout");
  assert.deepEqual(game.watchdog("3"), []);
  assert.equal(first.timeoutMs, 2000);
});

test("validates readiness and disconnect resets a reconnect to a fresh 60-step game", () => {
  const game = new Game(() => 0);
  assert.throws(() => game.acceptReady({ ...ready, supportedActions: [] }), /invalid board.ready/);
  game.acceptReady(ready);
  game.start();
  game.disconnect();
  assert.deepEqual(game.snapshot(), { boards: [], started: false });
  assert.deepEqual(game.watchdog("1"), []);

  game.acceptReady({ ...ready, supportedActions: ["press"] });
  let effects = game.start();
  for (let step = 1; step <= TOTAL_STEPS; step += 1) {
    const instruction = sendEffect(effects);
    assert.equal(instruction.roundId, String(step));
    assert.equal(instruction.action, "press");
    effects = game.actionDetected({
      type: "action.detected",
      roundId: String(step),
      action: "press",
      elapsedMs: 0,
    });
    effects = step < TOTAL_STEPS ? game.advance(String(step)) : effects;
  }
  assert.equal(game.score, TOTAL_STEPS);
  assert.equal(game.status, "complete");
});
