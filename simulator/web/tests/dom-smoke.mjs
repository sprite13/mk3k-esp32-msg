import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';

import {
  COLORS,
  drawFirmwareFrame,
  paintGlyph,
} from '../src/device-renderer.mjs';
import {
  DISPLAY_PLAN_SIZE,
  FONT_GLYPH_SIZE,
  SNAPSHOT_SIZE,
} from '../src/wasm-client.mjs';

class FakeContext {
  constructor() {
    this.fillStyle = '';
    this.fills = [];
  }
  save() {}
  restore() {}
  translate() {}
  rotate() {}
  fillRect(x, y, width, height) {
    this.fills.push({ color: this.fillStyle, x, y, width, height });
  }
}

test('page declares the complete accessible simulator controls', async () => {
  const html = await readFile(new URL('../index.html', import.meta.url), 'utf8');
  assert.match(html, /name="robots" content="noindex,nofollow"/);
  assert.match(html, /<canvas[^>]+id="device-screen"[^>]+width="135"[^>]+height="240"/s);
  for (const id of ['button-a', 'button-b', 'notification-form', 'connection',
    'battery', 'battery-value', 'battery-unknown', 'previews-hidden',
    'audio-enabled', 'reset-simulator', 'retry-start']) {
    assert.match(html, new RegExp(`id="${id}"`));
  }
  for (const name of ['title', 'subtitle', 'message']) {
    assert.match(html, new RegExp(`name="${name}"`));
  }
  assert.match(html, /не эмулирует BLE\/ANCS/i);
  assert.match(html, /расход батареи/i);
  assert.match(html, /<script type="module" src="\.\/src\/app\.mjs"><\/script>/);
  assert.match(html, /id="battery"[^>]+type="range"[^>]+min="0"[^>]+max="100"/);
  assert.match(html, /id="retry-start"[^>]+hidden/);
  assert.doesNotMatch(html, /(?:src|href)="\//);
  assert.doesNotMatch(html, /https?:\/\//);
});

test('responsive CSS has 44px targets and switches at 760px', async () => {
  const css = await readFile(new URL('../styles/main.css', import.meta.url), 'utf8');
  assert.match(css, /min-height:\s*44px/);
  assert.match(css, /@media\s*\(min-width:\s*760px\)/);
  assert.match(css, /:focus-visible/);
  assert.doesNotMatch(css, /@import|https?:\/\//);
});

test('all eight approved scenarios are available', async () => {
  const { UI_SCENARIOS } = await import('../src/scenarios.mjs');
  assert.deepEqual(Object.keys(UI_SCENARIOS), [
    'normal', 'title-only', 'message-only', 'empty', 'long', 'six-message-burst',
    'connection-loss-recovery', 'hidden-previews',
  ]);
});

test('WASM bridge sizes match the explicit C ABI', () => {
  assert.equal(SNAPSHOT_SIZE, 528);
  assert.equal(DISPLAY_PLAN_SIZE, 504);
  assert.equal(FONT_GLYPH_SIZE, 40);
});

test('bitmap painter plots set pixels and never uses browser text', () => {
  const context = new FakeContext();
  const rows = new Uint16Array(20);
  rows[0] = 0x800;
  rows[19] = 0x001;
  paintGlyph(context, 3, 4, rows, '#fff');
  assert.deepEqual(context.fills, [
    { color: '#fff', x: 3, y: 4, width: 1, height: 1 },
    { color: '#fff', x: 14, y: 23, width: 1, height: 1 },
  ]);
});

test('screen-off frame is entirely black', () => {
  const context = new FakeContext();
  drawFirmwareFrame(context, { displayAwake: false }, [], () => null);
  assert.deepEqual(context.fills, [
    { color: COLORS.off, x: 0, y: 0, width: 240, height: 135 },
  ]);
});

test('warning line uses the low-battery warning palette', () => {
  const context = new FakeContext();
  drawFirmwareFrame(context, { displayAwake: true, connection: 1 }, [
    { style: 1, glyphs: [65] },
  ], () => {
    const rows = new Uint16Array(20);
    rows[0] = 0x800;
    return rows;
  });
  assert.ok(context.fills.some((fill) => fill.color === COLORS.warning));
});

test('WASM module and binary resolve relative to the client module URL', async () => {
  const { wasmAssetUrls } = await import('../src/wasm-client.mjs');
  const base = new URL('https://example.test/mk3k-esp32-preview/src/wasm-client.mjs');
  const urls = wasmAssetUrls(base);
  assert.equal(urls.module.href,
    'https://example.test/mk3k-esp32-preview/dist/mk3k-simulator.mjs');
  assert.equal(urls.wasm.href,
    'https://example.test/mk3k-esp32-preview/dist/mk3k-simulator.wasm');
});

test('application contains no persistence, service worker, or network submission API', async () => {
  const files = ['app.mjs', 'audio.mjs', 'device-renderer.mjs', 'interactions.mjs',
    'scenarios.mjs', 'wasm-client.mjs'];
  const source = (await Promise.all(files.map((file) =>
    readFile(new URL(`../src/${file}`, import.meta.url), 'utf8')))).join('\n');
  assert.doesNotMatch(source,
    /localStorage|sessionStorage|indexedDB|serviceWorker|XMLHttpRequest|WebSocket/);
  assert.doesNotMatch(source, /\bfetch\s*\(/);
});
