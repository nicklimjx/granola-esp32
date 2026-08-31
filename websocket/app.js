import { Game } from "./game.mjs";

const SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
const INSTRUCTION_TYPE = 0x21;
const ROUND_ENDED_TYPE = 0x22;
const STOP_TYPE = 0x24;
const bluetoothSupported = "bluetooth" in navigator;
const promptSounds = Object.fromEntries(
  ["tap", "swipe", "press"].map(action => [action, new Audio(`/audio/${action}.mp3`)]),
);
let playingSound;

const boards = document.querySelector("#boards");
const empty = document.querySelector("#empty");
const summary = document.querySelector("#summary");
const connect = document.querySelector("#connect");
const start = document.querySelector("#start");
const stop = document.querySelector("#stop");
const restart = document.querySelector("#restart");
let decoder = new TextDecoder();
const game = new Game();
let device;
let rx;
let incoming = "";
let writes = Promise.resolve();
let generation = 0;
let gameGeneration = 0;
let connecting = false;
const timers = new Set();

function render() {
  const state = game.snapshot();
  boards.replaceChildren(...state.boards.map(board => {
    const tile = document.createElement("section");
    tile.className = `board ${board.status}`;
    const title = document.createElement("h2");
    title.textContent = board.boardId;
    const status = document.createElement("span");
    status.className = "status";
    status.textContent = board.status;
    const instruction = document.createElement("p");
    instruction.className = "instruction";
    instruction.textContent = board.instruction?.action || (board.status === "complete" ? "Finished" : "Ready");
    const score = document.createElement("div");
    score.className = "score";
    const reaction = board.elapsedMs === undefined ? "" : ` in ${board.elapsedMs} ms`;
    const result = board.lastVerdict ? ` · ${board.lastVerdict.replace("_", " ")}${reaction}` : "";
    score.textContent = `Score ${board.score}${result}`;
    tile.append(title, status, instruction, score);
    return tile;
  }));
  empty.hidden = state.boards.length > 0;
  connect.textContent = device?.gatt?.connected ? "Board connected" : connecting ? "Connecting..." : "Connect board";
  connect.disabled = !bluetoothSupported || connecting || Boolean(device?.gatt?.connected);
  const boardStatus = state.boards[0]?.status;
  start.disabled = state.started || state.boards.length === 0 || !device?.gatt?.connected || boardStatus === "stopped" || boardStatus === "complete";
  start.textContent = state.started ? "Game started" : "Start game";
  stop.disabled = !state.started || !device?.gatt?.connected;
  restart.disabled = !device?.gatt?.connected || (boardStatus !== "stopped" && boardStatus !== "complete");
  summary.textContent = !bluetoothSupported
    ? "Web Bluetooth requires Chromium on localhost"
    : state.boards.length
      ? `${state.boards[0].status} - one board`
      : "Connect a board over Bluetooth";
}

function stopPromptSound() {
  if (!playingSound) return;
  playingSound.pause();
  playingSound.currentTime = 0;
  playingSound = undefined;
}

function playPromptSound(action) {
  stopPromptSound();
  const sound = promptSounds[action];
  if (!sound) return;
  sound.currentTime = 0;
  playingSound = sound;
  sound.play().catch(error => console.warn(`Could not play ${action} prompt`, error));
}

function invalidateTransport() {
  generation += 1;
  for (const timer of timers) clearTimeout(timer);
  timers.clear();
  incoming = "";
  decoder = new TextDecoder();
  writes = Promise.resolve();
  rx = undefined;
  return generation;
}

function invalidateGameEffects() {
  gameGeneration += 1;
  for (const timer of timers) clearTimeout(timer);
  timers.clear();
}

function failTransport(error, failedGeneration, label = "Bluetooth write failed") {
  if (failedGeneration !== generation) return;
  const failedDevice = device;
  invalidateTransport();
  stopPromptSound();
  game.disconnect();
  device = undefined;
  connecting = false;
  if (failedDevice?.gatt?.connected) {
    try { failedDevice.gatt.disconnect(); } catch (disconnectError) { console.warn(disconnectError); }
  }
  render();
  console.error(error);
  summary.textContent = `${label}: ${error.message}`;
}

function run(effects) {
  const effectGeneration = generation;
  const effectGameGeneration = gameGeneration;
  let precedingWrite = Promise.resolve();
  for (const effect of effects) {
    if (effect.type === "send") {
      precedingWrite = queueWrite(effect.message);
      if (effect.message.type === "instruction") {
        precedingWrite.then(() => {
          if (effectGeneration !== generation || effectGameGeneration !== gameGeneration) return;
          playPromptSound(effect.message.action);
        }).catch(() => {});
      }
      precedingWrite.catch(error => failTransport(error, effectGeneration));
    } else if (effect.type === "audio.stop") {
      if (effectGeneration === generation && effectGameGeneration === gameGeneration) stopPromptSound();
    } else if (effect.type === "schedule") {
      precedingWrite.then(() => {
        if (effectGeneration !== generation || effectGameGeneration !== gameGeneration) return;
        const timer = setTimeout(() => {
          timers.delete(timer);
          if (effectGeneration !== generation || effectGameGeneration !== gameGeneration) return;
          const next = effect.event === "watchdog"
            ? game.watchdog(effect.session, effect.round)
            : game.advance(effect.session, effect.round);
          run(next);
          render();
        }, effect.delayMs);
        timers.add(timer);
      }).catch(() => {});
    }
  }
  render();
}

function encodeGameplayPacket(message) {
  const length = message.type === "instruction" ? 9 : message.type === "game.stop" ? 6 : 0;
  if (!length) throw new Error(`unsupported gameplay message: ${message.type}`);
  const bytes = new Uint8Array(length);
  const view = new DataView(bytes.buffer);
  bytes[0] = message.type === "instruction" ? INSTRUCTION_TYPE : STOP_TYPE;
  view.setUint32(1, message.session, true);
  if (message.type === "instruction") {
    bytes[5] = message.round;
    bytes[6] = message.actionCode;
    view.setUint16(7, message.timeoutMs, true);
  } else {
    bytes[5] = message.reset ? 1 : 0;
  }
  return bytes;
}

function queueWrite(message) {
  const characteristic = rx;
  const writeGeneration = generation;
  const writeGameGeneration = gameGeneration;
  const isControl = message.type === "game.stop";
  const bytes = encodeGameplayPacket(message);
  const operation = writes.then(async () => {
    if (!characteristic || writeGeneration !== generation) throw new Error("stale Bluetooth write");
    if (!isControl && writeGameGeneration !== gameGeneration) return;
    await characteristic.writeValueWithResponse(bytes);
  });
  writes = operation.catch(() => {});
  return operation;
}

function receive(value, receiveGeneration) {
  if (receiveGeneration !== generation) return;
  if (value.byteLength === 9 && value.getUint8(0) === ROUND_ENDED_TYPE) {
    run(game.roundEnded({
      type: "round.ended",
      session: value.getUint32(1, true),
      round: value.getUint8(5),
      actionCode: value.getUint8(6),
      elapsedMs: value.getUint16(7, true),
    }));
    render();
    return;
  }
  incoming += decoder.decode(value, { stream: true });
  for (;;) {
    const newline = incoming.indexOf("\n");
    if (newline < 0) return;
    const line = incoming.slice(0, newline).trim();
    incoming = incoming.slice(newline + 1);
    if (!line) continue;
    try {
      const message = JSON.parse(line);
      if (message.type === "board.ready") game.acceptReady(message);
      render();
    } catch (error) {
      console.error("Invalid board message", error, line);
    }
  }
}

connect.addEventListener("click", async () => {
  const attemptGeneration = invalidateTransport();
  game.disconnect();
  device = undefined;
  connecting = true;
  render();
  try {
    const selectedDevice = await navigator.bluetooth.requestDevice({ filters: [{ services: [SERVICE_UUID] }] });
    if (attemptGeneration !== generation) return;
    device = selectedDevice;
    selectedDevice.addEventListener("gattserverdisconnected", () => {
      if (device !== selectedDevice) return;
      failTransport(new Error("board disconnected"), attemptGeneration, "Bluetooth disconnected");
    });
    const server = await selectedDevice.gatt.connect();
    const service = await server.getPrimaryService(SERVICE_UUID);
    rx = await service.getCharacteristic(RX_UUID);
    const tx = await service.getCharacteristic(TX_UUID);
    tx.addEventListener("characteristicvaluechanged", event => receive(event.target.value, attemptGeneration));
    await tx.startNotifications();
    if (attemptGeneration !== generation) return;
    connecting = false;
    render();
  } catch (error) {
    failTransport(error, attemptGeneration, "Bluetooth connection failed");
  }
});

start.addEventListener("click", () => run(game.start()));
stop.addEventListener("click", () => {
  invalidateGameEffects();
  stopPromptSound();
  run(game.stop());
});
restart.addEventListener("click", () => {
  invalidateGameEffects();
  run(game.restart());
});

render();
