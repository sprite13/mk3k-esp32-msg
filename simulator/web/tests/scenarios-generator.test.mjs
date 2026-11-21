import assert from 'node:assert/strict';
import test from 'node:test';

import { cStringLiteral } from './scenarios.mjs';

test('escapes quotes, backslashes, controls, and embedded NUL as fixed octal bytes', () => {
  assert.equal(cStringLiteral('"\\\0\n\x1f'), '"\\042\\134\\000\\012\\037"');
});

test('encodes non-BMP Unicode as UTF-8 bytes', () => {
  assert.equal(cStringLiteral('\u{1f600}'), '"\\360\\237\\230\\200"');
});

test('replaces lone surrogates with the UTF-8 replacement character', () => {
  assert.equal(cStringLiteral('\ud800'), '"\\357\\277\\275"');
  assert.equal(cStringLiteral('\udc00'), '"\\357\\277\\275"');
});

test('fixed-width escapes cannot consume following hexadecimal or octal digits', () => {
  assert.equal(cStringLiteral('A9f'), '"\\101\\071\\146"');
});
