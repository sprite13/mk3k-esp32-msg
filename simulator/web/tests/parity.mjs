import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';
import { scenarios } from './scenarios.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const distModule = resolve(here, '../dist/mk3k-simulator.mjs');
const createModule = (await import(distModule)).default;
const nativeSnapshots = JSON.parse(await readFile(resolve(here, '../artifacts/native-scenarios.json'), 'utf8'));
const module = await createModule({ locateFile: (name) => resolve(here, '../dist', name) });
const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });

const SNAPSHOT_SIZE = 528;
const OFFSETS = { abi: 0, connection: 4, battery: 5, position: 6, total: 7, awake: 8,
  present: 9, beep: 10, uid: 12, appId: 16, title: 80, subtitle: 176, message: 272 };

function cString(offset, capacity) {
  const bytes = module.HEAPU8.subarray(offset, offset + capacity);
  const end = bytes.indexOf(0);
  return decoder.decode(bytes.subarray(0, end < 0 ? capacity : end));
}

function snapshot() {
  const pointer = module._malloc(SNAPSHOT_SIZE);
  try {
    assert.equal(module._simulator_snapshot(pointer), 0);
    const view = new DataView(module.HEAPU8.buffer, pointer, SNAPSHOT_SIZE);
    return {
      abiVersion: view.getUint32(OFFSETS.abi, true), connection: view.getUint8(OFFSETS.connection),
      batteryPercent: view.getUint8(OFFSETS.battery), position: view.getUint8(OFFSETS.position),
      total: view.getUint8(OFFSETS.total), displayAwake: view.getUint8(OFFSETS.awake),
      notificationPresent: view.getUint8(OFFSETS.present), beepRequested: view.getUint8(OFFSETS.beep),
      notification: { uid: view.getUint32(OFFSETS.uid, true),
        appId: cString(pointer + OFFSETS.appId, 64), title: cString(pointer + OFFSETS.title, 96),
        subtitle: cString(pointer + OFFSETS.subtitle, 96), message: cString(pointer + OFFSETS.message, 256) },
    };
  } finally { module._free(pointer); }
}

function withCString(value, callback) {
  const bytes = encoder.encode(value);
  const pointer = module._malloc(bytes.length + 1);
  module.HEAPU8.set(bytes, pointer); module.HEAPU8[pointer + bytes.length] = 0;
  try { return callback(pointer); } finally { module._free(pointer); }
}

function runAction(action) {
  const at = action.at ?? 0n;
  switch (action.type) {
    case 'reset': return module._simulator_reset(at);
    case 'connect': return module._simulator_set_connection(1, at);
    case 'tick': return module._simulator_tick(at);
    case 'button-a': return module._simulator_button_a(at);
    case 'button-b-down': return module._simulator_button_b_down(at);
    case 'button-b-up': return module._simulator_button_b_up(at);
    case 'battery': return module._simulator_set_battery(action.percent, at);
    case 'previews-hidden': return module._simulator_set_previews_hidden(action.hidden ? 1 : 0);
    case 'notify': return withCString(action.title, (title) => withCString(action.subtitle, (subtitle) =>
      withCString(action.message, (message) => module._simulator_send_notification(title, subtitle, message, at))));
    default: throw new Error(`unknown action: ${action.type}`);
  }
}

const wasmSnapshots = scenarios.map(({ name, actions }) => ({ name, steps: actions.map((action) => {
  const result = runAction(action);
  return { result, snapshot: snapshot() };
}) }));

assert.deepEqual(wasmSnapshots, nativeSnapshots);
for (const scenario of wasmSnapshots) console.log(`PASS ${scenario.name}`);
console.log(`native/WASM parity: ${wasmSnapshots.length} scenarios`);
