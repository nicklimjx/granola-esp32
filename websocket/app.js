import { Game } from "./game.mjs";

const SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
const RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
const TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
const CHUNK_BYTES = 20;
const bluetoothSupported = "bluetooth" in navigator;

const boards = document.querySelector("#boards");
const empty = document.querySelector("#empty");
const summary = document.querySelector("#summary");
const connect = document.querySelector("#connect");
const start = document.querySelector("#start");
const stop = document.querySelector("#stop");
const restart = document.querySelector("#restart");
let decoder = new TextDecoder();
const encoder = new TextEncoder();
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

function run(effects) {
  const effectGeneration = generation;
  const effectGameGeneration = gameGeneration;
  let precedingWrite = Promise.resolve();
  for (const effect of effects) {
    if (effect.type === "send") {
      precedingWrite = queueWrite(effect.message);
      precedingWrite.catch(error => {
        if (effectGeneration !== generation) return;
        console.error(error);
        summary.textContent = `Bluetooth write failed: ${error.message}`;
      });
    } else {
      precedingWrite.then(() => {
        if (effectGeneration !== generation || effectGameGeneration !== gameGeneration) return;
        const timer = setTimeout(() => {
          timers.delete(timer);
          if (effectGeneration !== generation || effectGameGeneration !== gameGeneration) return;
          const next = effect.event === "watchdog"
            ? game.watchdog(effect.roundId)
            : game.advance(effect.roundId);
          run(next);
          render();
        }, effect.delayMs);
        timers.add(timer);
      }).catch(() => {});
    }
  }
  render();
}

function queueWrite(message) {
  const characteristic = rx;
  const writeGeneration = generation;
  const writeGameGeneration = gameGeneration;
  const isControl = message.type === "game.stop";
  const bytes = encoder.encode(`${JSON.stringify(message)}\n`);
  const operation = writes.then(async () => {
    if (!characteristic || writeGeneration !== generation) throw new Error("stale Bluetooth write");
    if (!isControl && writeGameGeneration !== gameGeneration) return;
    // Once a newline-framed message starts, finish every chunk so the board
    // never receives a partial frame. Generation checks belong before byte 0.
    for (let offset = 0; offset < bytes.length; offset += CHUNK_BYTES) {
      await characteristic.writeValueWithResponse(bytes.slice(offset, offset + CHUNK_BYTES));
    }
  });
  writes = operation.catch(() => {});
  return operation;
}

function receive(value, receiveGeneration) {
  if (receiveGeneration !== generation) return;
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
      else if (message.type === "action.detected") run(game.actionDetected(message));
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
      device = undefined;
      connecting = false;
      invalidateTransport();
      game.disconnect();
      render();
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
    if (attemptGeneration !== generation) return;
    console.error(error);
    invalidateTransport();
    game.disconnect();
    device = undefined;
    connecting = false;
    render();
    summary.textContent = `Bluetooth connection failed: ${error.message}`;
  }
});

start.addEventListener("click", () => run(game.start()));
stop.addEventListener("click", () => {
  invalidateGameEffects();
  run(game.stop());
});
restart.addEventListener("click", () => {
  invalidateGameEffects();
  run(game.restart());
});

render();
