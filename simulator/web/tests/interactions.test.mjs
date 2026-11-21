import assert from 'node:assert/strict';
import test from 'node:test';

import { bindDeviceButtons } from '../src/interactions.mjs';

class FakeButton {
  listeners = new Map();
  captured = null;
  classList = { add: () => {}, remove: () => {} };
  addEventListener(type, callback) { this.listeners.set(type, callback); }
  setPointerCapture(pointerId) { this.captured = pointerId; }
  emit(type, event = {}) { this.listeners.get(type)?.(event); }
}

test('B emits one down and one up even when pointer capture is cancelled', () => {
  const buttonA = new FakeButton();
  const buttonB = new FakeButton();
  const events = [];
  bindDeviceButtons({ buttonA, buttonB, onA: () => events.push('a'),
    onBDown: () => events.push('down'), onBUp: () => events.push('up') });
  buttonB.emit('pointerdown', { pointerId: 7 });
  buttonB.emit('pointerdown', { pointerId: 7 });
  buttonB.emit('pointercancel');
  buttonB.emit('lostpointercapture');
  assert.equal(buttonB.captured, 7);
  assert.deepEqual(events, ['down', 'up']);
});

test('A click and keyboard B preserve distinct actions', () => {
  const buttonA = new FakeButton();
  const buttonB = new FakeButton();
  const events = [];
  bindDeviceButtons({ buttonA, buttonB, onA: () => events.push('a'),
    onBDown: () => events.push('down'), onBUp: () => events.push('up') });
  buttonA.emit('click');
  buttonB.emit('keydown', { key: 'Enter', repeat: false });
  buttonB.emit('keyup', { key: 'Enter' });
  assert.deepEqual(events, ['a', 'down', 'up']);
});
