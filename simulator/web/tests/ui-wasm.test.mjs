import assert from 'node:assert/strict';
import test from 'node:test';

import { loadWasmClient } from '../src/wasm-client.mjs';

test('compiled WASM exposes the shared display plan and bitmap font', async () => {
  const client = await loadWasmClient();
  client.reset(0n);
  client.setConnection(1, 1n);
  client.setBattery(10, 2n);
  client.sendNotification({ title: 'Алиса', subtitle: 'Чат', message: 'Привет' }, 3n);
  const snapshot = client.snapshot();
  const plan = client.displayPlan();
  assert.equal(snapshot.notification.title, 'Алиса');
  assert.equal(plan.length, 6);
  assert.equal(plan[0].style, 1);
  assert.equal(plan[1].style, 2);
  assert.equal(plan[1].glyphs[0], 0x0410);
  assert.ok(Array.from(client.fontGlyph(0x0410)).some((row) => row !== 0));
  assert.equal(client.takeBeep(), true);
  assert.equal(client.takeBeep(), false);
});
