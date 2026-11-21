import assert from 'node:assert/strict';
import test from 'node:test';

import { readBatteryControl } from '../src/controls.mjs';
import { createFrameScheduler } from '../src/frame-scheduler.mjs';
import { createStartupController } from '../src/startup-controller.mjs';

test('battery control covers 0, 42, 100 and explicit unknown', () => {
  const range = { value: '0' };
  const unknown = { checked: false };
  assert.equal(readBatteryControl(range, unknown), 0);
  range.value = '42';
  assert.equal(readBatteryControl(range, unknown), 42);
  range.value = '100';
  assert.equal(readBatteryControl(range, unknown), 100);
  unknown.checked = true;
  assert.equal(readBatteryControl(range, unknown), 255);
});

test('frame scheduler coalesces changes and draws latest state in one rAF', () => {
  const callbacks = [];
  const rendered = [];
  const scheduler = createFrameScheduler((callback) => callbacks.push(callback),
    (frame) => rendered.push(frame));
  scheduler.schedule({ value: 1 });
  scheduler.schedule({ value: 2 });
  scheduler.schedule({ value: 3 });
  assert.equal(callbacks.length, 1);
  assert.deepEqual(rendered, []);
  callbacks.shift()(10);
  assert.deepEqual(rendered, [{ value: 3 }]);
  scheduler.schedule({ value: 4 });
  assert.equal(callbacks.length, 1);
});

test('retry aborts prior attempt and stale success cannot become running', async () => {
  const starts = [];
  const states = [];
  const pending = [];
  const controller = createStartupController({
    start: (signal) => {
      starts.push(signal);
      return new Promise((resolve, reject) => pending.push({ resolve, reject }));
    },
    onState: (state, error) => states.push([state, error?.message]),
  });
  const first = controller.attempt();
  const second = controller.attempt();
  assert.equal(starts[0].aborted, true);
  assert.equal(starts[1].aborted, false);
  pending[0].resolve();
  pending[1].resolve();
  assert.equal(await first, false);
  assert.equal(await second, true);
  assert.deepEqual(states.map(([state]) => state), ['loading', 'loading', 'running']);
});

test('failed startup exposes failed state and retry can reach running', async () => {
  let attempts = 0;
  const states = [];
  const controller = createStartupController({
    start: async () => {
      attempts += 1;
      if (attempts === 1) throw new Error('WASM не загрузился');
    },
    onState: (state, error) => states.push([state, error?.message]),
  });
  assert.equal(await controller.attempt(), false);
  assert.equal(await controller.attempt(), true);
  assert.deepEqual(states, [
    ['loading', undefined], ['failed', 'WASM не загрузился'],
    ['loading', undefined], ['running', undefined],
  ]);
});
