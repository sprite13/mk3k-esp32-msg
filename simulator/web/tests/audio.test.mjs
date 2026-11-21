import assert from 'node:assert/strict';
import test from 'node:test';

import { BeepAudio } from '../src/audio.mjs';

test('failed resume rolls back enabled state and closes partial context', async () => {
  const contexts = [];
  class FailingContext {
    constructor() { contexts.push(this); this.closed = false; }
    async resume() { throw new Error('blocked'); }
    async close() { this.closed = true; }
  }
  const audio = new BeepAudio({ AudioContextClass: FailingContext });
  await assert.rejects(audio.setEnabled(true), /blocked/);
  assert.equal(audio.enabled, false);
  assert.equal(contexts[0].closed, true);
  assert.doesNotThrow(() => audio.play());
});

test('disable wins over a stale pending enable', async () => {
  let releaseResume;
  const contexts = [];
  class DeferredContext {
    constructor() { contexts.push(this); this.closed = 0; }
    resume() { return new Promise((resolve) => { releaseResume = resolve; }); }
    async close() { this.closed += 1; }
  }
  const audio = new BeepAudio({ AudioContextClass: DeferredContext });
  const enabling = audio.setEnabled(true);
  await audio.setEnabled(false);
  releaseResume();
  await enabling;
  assert.equal(audio.enabled, false);
  assert.ok(contexts[0].closed >= 1);
});

test('dispose invalidates a pending enable and leaves no live context', async () => {
  let releaseResume;
  class DeferredContext {
    constructor() { this.closed = false; }
    resume() { return new Promise((resolve) => { releaseResume = resolve; }); }
    async close() { this.closed = true; }
  }
  const audio = new BeepAudio({ AudioContextClass: DeferredContext });
  const enabling = audio.setEnabled(true);
  await audio.dispose();
  releaseResume();
  await enabling;
  assert.equal(audio.enabled, false);
});
