import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

class FakeElement {
  constructor() {
    this.handlers = new Map();
    this.children = [];
    this.hidden = false;
    this.disabled = false;
    this.textContent = "";
  }

  addEventListener(type, handler) { this.handlers.set(type, handler); }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
}

function deferred() {
  let resolve;
  const promise = new Promise(done => { resolve = done; });
  return { promise, resolve };
}

const flushPromises = () => new Promise(resolve => setImmediate(resolve));
const asView = bytes => new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);

function roundPacket(session, round, actionCode, elapsedMs) {
  const bytes = new Uint8Array(9);
  const view = new DataView(bytes.buffer);
  bytes[0] = 0x22;
  view.setUint32(1, session, true);
  bytes[5] = round;
  bytes[6] = actionCode;
  view.setUint16(7, elapsedMs, true);
  return view;
}

test("uses atomic v2 packets and ties prompt audio to the active round", async () => {
  const elements = new Map(
    ["boards", "empty", "summary", "connect", "start", "stop", "restart"]
      .map(id => [`#${id}`, new FakeElement()]),
  );
  const audioPlays = [];
  const audioPauses = [];
  const pendingWrites = [];
  const writes = [];
  let notificationHandler;
  let rejectNextInstruction = false;
  let disconnectCalls = 0;
  const consoleErrors = [];
  const originalConsoleError = console.error;
  console.error = (...args) => consoleErrors.push(args);

  class FakeAudio {
    constructor(url) {
      this.url = url;
      this.currentTime = 0;
    }
    play() {
      audioPlays.push(this.url);
      return Promise.resolve();
    }
    pause() { audioPauses.push(this.url); }
  }

  const rx = {
    writeValueWithResponse(bytes) {
      const written = new Uint8Array(bytes);
      writes.push(written);
      if (rejectNextInstruction && written[0] === 0x21) {
        rejectNextInstruction = false;
        return Promise.reject(new Error("instruction rejected"));
      }
      const gate = deferred();
      pendingWrites.push(gate);
      return gate.promise;
    },
  };
  const tx = {
    addEventListener(type, handler) {
      assert.equal(type, "characteristicvaluechanged");
      notificationHandler = handler;
    },
    startNotifications: () => Promise.resolve(),
  };
  const service = {
    getCharacteristic(uuid) {
      return Promise.resolve(uuid.endsWith("0002-b5a3-f393-e0a9-e50e24dcca9e") ? rx : tx);
    },
  };
  const device = {
    gatt: {
      connected: true,
      connect: () => Promise.resolve({ getPrimaryService: () => Promise.resolve(service) }),
      disconnect() {
        disconnectCalls += 1;
        this.connected = false;
      },
    },
    addEventListener() {},
  };

  Object.defineProperty(globalThis, "Audio", { configurable: true, value: FakeAudio });
  Object.defineProperty(globalThis, "document", {
    configurable: true,
    value: {
      querySelector: selector => elements.get(selector),
      createElement: () => new FakeElement(),
    },
  });
  Object.defineProperty(globalThis, "navigator", {
    configurable: true,
    value: { bluetooth: { requestDevice: () => Promise.resolve(device) } },
  });

  const appPath = new URL("./app.js", import.meta.url);
  const gameUrl = new URL("./game.mjs", import.meta.url).href;
  const source = (await readFile(appPath, "utf8")).replace('"./game.mjs"', `"${gameUrl}"`);
  await import(`data:text/javascript;base64,${Buffer.from(source).toString("base64")}#v2`);

  await elements.get("#connect").handlers.get("click")();
  const readyBytes = new TextEncoder().encode(`${JSON.stringify({
    type: "board.ready",
    protocolVersion: 2,
    boardId: "test-board",
    supportedActions: ["tap"],
  })}\n`);
  notificationHandler({ target: { value: asView(readyBytes.subarray(0, 13)) } });
  assert.equal(elements.get("#start").disabled, true, "partial ready frame must not connect the game");
  notificationHandler({ target: { value: asView(readyBytes.subarray(13)) } });
  assert.equal(elements.get("#start").disabled, false);

  elements.get("#start").handlers.get("click")();
  await flushPromises();
  assert.equal(writes.length, 1, "instruction must be one GATT operation");
  assert.deepEqual([...writes[0]], [0x21, 2, 0, 0, 0, 1, 1, 0xa0, 0x0f]);
  assert.equal(pendingWrites.length, 1);
  assert.deepEqual(audioPlays, [], "cue fired before the instruction write completed");

  pendingWrites.shift().resolve();
  await flushPromises();
  assert.deepEqual(audioPlays, ["/audio/tap.mp3"]);

  notificationHandler({ target: { value: roundPacket(999, 1, 1, 50) } });
  assert.deepEqual(audioPauses, [], "stale session event stopped the active cue");
  assert.equal(writes.length, 1);

  notificationHandler({ target: { value: roundPacket(2, 1, 1, 50) } });
  assert.deepEqual(audioPauses, ["/audio/tap.mp3"]);
  assert.equal(writes.length, 1, "round completion must not write round.result");
  const scoreText = elements.get("#boards").children[0].children[3].textContent;
  assert.equal(scoreText, "Score 1 · success in 50 ms");

  elements.get("#stop").handlers.get("click")();
  await flushPromises();
  assert.equal(writes.length, 2, "stop must be one additional GATT operation");
  assert.deepEqual([...writes[1]], [0x24, 2, 0, 0, 0, 0]);
  pendingWrites.shift().resolve();
  await flushPromises();

  elements.get("#restart").handlers.get("click")();
  await flushPromises();
  assert.deepEqual([...writes[2]], [0x24, 2, 0, 0, 0, 1], "restart stop must target old session");
  rejectNextInstruction = true;
  pendingWrites.shift().resolve();
  await flushPromises();
  assert.deepEqual([...writes[3]], [0x21, 3, 0, 0, 0, 1, 1, 0xa0, 0x0f]);
  assert.equal(writes[3].length, 9);
  assert.equal(disconnectCalls, 1, "rejected gameplay write must disconnect GATT");
  assert.equal(elements.get("#boards").children.length, 0, "failed transport must clear Game state");
  assert.equal(elements.get("#connect").disabled, false, "failed transport must allow reconnect");
  assert.equal(elements.get("#connect").textContent, "Connect board");
  assert.equal(elements.get("#start").disabled, true);
  assert.equal(elements.get("#summary").textContent, "Bluetooth write failed: instruction rejected");
  assert.equal(consoleErrors.at(-1)[0].message, "instruction rejected");
  assert.deepEqual(audioPlays, ["/audio/tap.mp3"], "rejected instruction must not play a cue");
  console.error = originalConsoleError;
});
