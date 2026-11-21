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

class FakeTarget extends FakeButton {
  hidden = false;
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
  buttonB.emit('keydown', { key: 'Enter', repeat: false, preventDefault: () => {} });
  buttonB.emit('keyup', { key: 'Enter', preventDefault: () => {} });
  assert.deepEqual(events, ['a', 'down', 'up']);
});

test('B releases on button blur, window blur, and hidden document', () => {
  const buttonA = new FakeButton();
  const buttonB = new FakeButton();
  const documentTarget = new FakeTarget();
  const windowTarget = new FakeTarget();
  const events = [];
  bindDeviceButtons({ buttonA, buttonB, documentTarget, windowTarget,
    onA: () => {}, onBDown: () => events.push('down'), onBUp: () => events.push('up') });
  buttonB.emit('pointerdown');
  buttonB.emit('blur');
  buttonB.emit('pointerdown');
  windowTarget.emit('blur');
  buttonB.emit('pointerdown');
  documentTarget.hidden = true;
  documentTarget.emit('visibilitychange');
  assert.deepEqual(events, ['down', 'up', 'down', 'up', 'down', 'up']);
});

test('keyboard B prevents default scrolling and releases on blur', () => {
  const buttonA = new FakeButton();
  const buttonB = new FakeButton();
  let prevented = 0;
  const events = [];
  bindDeviceButtons({ buttonA, buttonB, documentTarget: new FakeTarget(),
    windowTarget: new FakeTarget(), onA: () => {}, onBDown: () => events.push('down'),
    onBUp: () => events.push('up') });
  buttonB.emit('keydown', { key: ' ', repeat: false, preventDefault: () => prevented++ });
  buttonB.emit('blur');
  assert.equal(prevented, 1);
  assert.deepEqual(events, ['down', 'up']);
});
