import assert from 'node:assert/strict';
import test from 'node:test';

import { WasmClient } from '../src/wasm-client.mjs';

function fakeModule(pointer = 128) {
  const heap = new Uint8Array(2048);
  const freed = [];
  const timestamps = [];
  return {
    HEAPU8: heap,
    freed,
    timestamps,
    _simulator_abi_version: () => 1,
    _malloc: () => pointer,
    _free: (value) => freed.push(value),
    _simulator_snapshot: (address) => {
      new DataView(heap.buffer).setUint32(address, 1, true);
      return 0;
    },
    _simulator_reset: (at) => { timestamps.push(at); return 0; },
  };
}

test('snapshot bytes are copied before the allocation is freed', () => {
  const module = fakeModule();
  const client = new WasmClient(module);
  const snapshot = client.snapshot();
  assert.equal(snapshot.abiVersion, 1);
  assert.deepEqual(module.freed, [128]);
  module.HEAPU8.fill(255, 128, 128 + 528);
  assert.equal(snapshot.abiVersion, 1);
});

test('zero malloc result fails without calling into or freeing WASM', () => {
  const module = fakeModule(0);
  const client = new WasmClient(module);
  assert.throws(() => client.snapshot(), /allocation failed/);
  assert.deepEqual(module.freed, []);
});

test('timestamps cross the i64 boundary as BigInt', () => {
  const module = fakeModule();
  const client = new WasmClient(module);
  client.reset(1234567890123n);
  assert.deepEqual(module.timestamps, [1234567890123n]);
});
