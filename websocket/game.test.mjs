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
  supportedActions: ["tap", "swipe", "press"],
};

function sendEffect(effects) {
  return effects.find(effect => effect.type === "send").message;
}

function sentMessages(effects) {
  return effects.filter(effect => effect.type === "send").map(effect => effect.message);
}

function succeed(game, effects, elapsedMs = 125) {
  const instruction = sendEffect(effects);
  return game.actionDetected({
    type: "action.detected",
    roundId: instruction.roundId,
    action: instruction.action,
    elapsedMs,
  });
}

test("plays exactly 60 deterministic steps across doubled timeout tiers and increments score", () => {
  assert.deepEqual(ACTION_WINDOWS_MS, [4000, 3400, 2800, 2400, 2000, 1600]);
  const game = new Game(() => 0.5);
  game.acceptReady(ready);
  let effects = game.start();

  for (let step = 1; step <= TOTAL_STEPS; step += 1) {
    const instruction = sendEffect(effects);
    assert.deepEqual(instruction, {
      type: "instruction",
      roundId: `1:${step}`,
      action: "swipe",
      timeoutMs: ACTION_WINDOWS_MS[Math.floor((step - 1) / 10)],
    });
    assert.deepEqual(effects[1], {
      type: "schedule",
      event: "watchdog",
      roundId: `1:${step}`,
      delayMs: instruction.timeoutMs + WATCHDOG_GRACE_MS,
    });

    effects = succeed(game, effects, instruction.timeoutMs);
    assert.deepEqual(sendEffect(effects), {
      type: "round.result",
      roundId: `1:${step}`,
      result: "success",
      score: step,
    });
    assert.equal(game.snapshot().boards[0].score, step);
    assert.equal(game.snapshot().boards[0].lastVerdict, "success");
    assert.equal(game.snapshot().boards[0].elapsedMs, instruction.timeoutMs);

    if (step < TOTAL_STEPS) {
      assert.deepEqual(effects[1], {
        type: "schedule",
        event: "advance",
        roundId: `1:${step}`,
        delayMs: FEEDBACK_HOLD_MS,
      });
      effects = game.advance(`1:${step}`);
    } else {
      assert.equal(effects.length, 1);
      assert.equal(game.status, "complete");
      assert.equal(game.started, false);
      assert.deepEqual(game.advance(`1:${step}`), []);
    }
  }
});

test("wrong, late, watchdog, and stale actions have exact observable outcomes", () => {
  const game = new Game(() => 0);
  game.acceptReady({ ...ready, supportedActions: ["tap"] });
  const first = sendEffect(game.start());

  let effects = game.actionDetected({ type: "action.detected", roundId: first.roundId, action: "twist", elapsedMs: 20 });
  assert.equal(sendEffect(effects).result, "wrong_action");
  assert.equal(game.score, 0);
  assert.deepEqual(game.snapshot().boards[0], {
    boardId: "bopit-01", status: "feedback", score: 0,
    lastVerdict: "wrong_action", elapsedMs: 20, instruction: undefined,
  });
  assert.deepEqual(game.actionDetected({ type: "action.detected", roundId: first.roundId, action: "tap", elapsedMs: 20 }), []);

  const second = sendEffect(game.advance(first.roundId));
  effects = game.actionDetected({ type: "action.detected", roundId: second.roundId, action: "tap", elapsedMs: second.timeoutMs + 1 });
  assert.equal(sendEffect(effects).result, "timeout");
  assert.equal(game.snapshot().boards[0].elapsedMs, second.timeoutMs + 1);

  const third = sendEffect(game.advance(second.roundId));
  effects = game.watchdog(third.roundId);
  assert.equal(sendEffect(effects).result, "timeout");
  assert.equal(game.snapshot().boards[0].elapsedMs, undefined);
  assert.deepEqual(game.watchdog(third.roundId), []);
  assert.equal(first.timeoutMs, 4000);
});

test("stop preserves score and restart resets a fresh run while stale events are harmless", () => {
  const game = new Game(() => 0);
  game.acceptReady(ready);
  const firstInstruction = sendEffect(game.start());
  let effects = succeed(game, [{ type: "send", message: firstInstruction }], 90);
  assert.equal(game.score, 1);
  const oldAdvance = effects[1];

  assert.deepEqual(game.stop(), [{ type: "send", message: { type: "game.stop", reset: false } }]);
  assert.equal(game.status, "stopped");
  assert.equal(game.score, 1);
  assert.equal(game.snapshot().boards[0].lastVerdict, "success");
  assert.deepEqual(game.advance(oldAdvance.roundId), []);
  assert.deepEqual(game.watchdog(firstInstruction.roundId), []);

  const restarted = game.restart();
  const [reset, newInstruction] = sentMessages(restarted);
  assert.deepEqual(reset, { type: "game.stop", reset: true });
  assert.equal(newInstruction.roundId, "2:1");
  assert.deepEqual(restarted[2], {
    type: "schedule", event: "watchdog", roundId: "2:1",
    delayMs: newInstruction.timeoutMs + WATCHDOG_GRACE_MS,
  });
  assert.equal(game.score, 0);
  assert.equal(game.step, 1);
  assert.equal(game.snapshot().boards[0].lastVerdict, undefined);
  assert.deepEqual(game.advance(oldAdvance.roundId), []);
  assert.deepEqual(game.watchdog(firstInstruction.roundId), []);
  assert.equal(game.active.roundId, newInstruction.roundId);
});

test("completion makes restart available and stale final-run events cannot affect it", () => {
  const game = new Game(() => 0);
  game.acceptReady({ ...ready, supportedActions: ["press"] });
  let effects = game.start();
  let lastRoundId;
  for (let step = 1; step <= TOTAL_STEPS; step += 1) {
    const instruction = sendEffect(effects);
    lastRoundId = instruction.roundId;
    effects = succeed(game, effects, 0);
    if (step < TOTAL_STEPS) effects = game.advance(instruction.roundId);
  }
  assert.equal(game.status, "complete");
  assert.equal(game.score, TOTAL_STEPS);

  const restartEffects = game.restart();
  assert.deepEqual(sentMessages(restartEffects)[0], { type: "game.stop", reset: true });
  const restarted = sentMessages(restartEffects)[1];
  assert.equal(restarted.roundId, "2:1");
  assert.equal(restarted.action, "press");
  assert.equal(game.status, "playing");
  assert.equal(game.score, 0);
  assert.deepEqual(game.watchdog(lastRoundId), []);
});

test("validates readiness and disconnect resets reconnect state", () => {
  const game = new Game(() => 0);
  assert.throws(() => game.acceptReady({ ...ready, supportedActions: [] }), /invalid board.ready/);
  game.acceptReady(ready);
  game.start();
  game.disconnect();
  assert.deepEqual(game.snapshot(), { boards: [], started: false });
  assert.deepEqual(game.watchdog("1:1"), []);

  game.acceptReady({ ...ready, supportedActions: ["press"] });
  const instruction = sendEffect(game.start());
  assert.equal(instruction.action, "press");
  assert.equal(instruction.roundId, "3:1");
});
