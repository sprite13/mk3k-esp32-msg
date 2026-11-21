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
