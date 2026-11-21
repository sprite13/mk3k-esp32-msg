export const SUPPORTED_ABI_VERSION = 1;
export const SNAPSHOT_SIZE = 528;
export const DISPLAY_PLAN_SIZE = 504;
export const FONT_GLYPH_SIZE = 40;

const SNAPSHOT_OFFSETS = Object.freeze({
  abi: 0, connection: 4, battery: 5, position: 6, total: 7, awake: 8,
  present: 9, beep: 10, uid: 12, appId: 16, title: 80, subtitle: 176,
  message: 272,
});
const LINE_SIZE = 84;
const LINE_GLYPHS_OFFSET = 4;
const DISPLAY_LINES = 6;
const DISPLAY_COLUMNS = 20;
const encoder = new TextEncoder();
const decoder = new TextDecoder('utf-8', { fatal: true });

export function wasmAssetUrls(base = import.meta.url) {
  return {
    module: new URL('../dist/mk3k-simulator.mjs', base),
    wasm: new URL('../dist/mk3k-simulator.wasm', base),
  };
}

function timestamp(value) {
  if (typeof value === 'bigint') return value;
  if (!Number.isSafeInteger(value) || value < 0) {
    throw new RangeError('timestamp must be a non-negative safe integer or BigInt');
  }
  return BigInt(value);
}

function cString(bytes, offset, capacity) {
  const field = bytes.subarray(offset, offset + capacity);
  const terminator = field.indexOf(0);
  return decoder.decode(field.subarray(0, terminator < 0 ? capacity : terminator));
}

function decodeSnapshot(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return Object.freeze({
    abiVersion: view.getUint32(SNAPSHOT_OFFSETS.abi, true),
    connection: view.getUint8(SNAPSHOT_OFFSETS.connection),
    batteryPercent: view.getUint8(SNAPSHOT_OFFSETS.battery),
    position: view.getUint8(SNAPSHOT_OFFSETS.position),
    total: view.getUint8(SNAPSHOT_OFFSETS.total),
    displayAwake: view.getUint8(SNAPSHOT_OFFSETS.awake) === 1,
    notificationPresent: view.getUint8(SNAPSHOT_OFFSETS.present) === 1,
    beepRequested: view.getUint8(SNAPSHOT_OFFSETS.beep) === 1,
    notification: Object.freeze({
      uid: view.getUint32(SNAPSHOT_OFFSETS.uid, true),
      appId: cString(bytes, SNAPSHOT_OFFSETS.appId, 64),
      title: cString(bytes, SNAPSHOT_OFFSETS.title, 96),
      subtitle: cString(bytes, SNAPSHOT_OFFSETS.subtitle, 96),
      message: cString(bytes, SNAPSHOT_OFFSETS.message, 256),
    }),
  });
}

function decodePlan(bytes) {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return Object.freeze(Array.from({ length: DISPLAY_LINES }, (_, line) => {
    const offset = line * LINE_SIZE;
    const length = view.getUint8(offset);
    if (length > DISPLAY_COLUMNS) throw new Error(`invalid display line length: ${length}`);
    const glyphs = Array.from({ length }, (_, column) =>
      view.getUint32(offset + LINE_GLYPHS_OFFSET + column * 4, true));
    return Object.freeze({ style: view.getUint8(offset + 1), glyphs: Object.freeze(glyphs) });
  }));
}

export class WasmClient {
  #module;
  #fontCache = new Map();

  constructor(module) {
    this.#module = module;
    const version = module._simulator_abi_version();
    if (version !== SUPPORTED_ABI_VERSION) {
      throw new Error(`unsupported simulator ABI ${version}; expected ${SUPPORTED_ABI_VERSION}`);
    }
  }

  #allocation(size, callback) {
    const pointer = this.#module._malloc(size);
    if (pointer === 0) throw new Error(`WASM allocation failed for ${size} bytes`);
    try {
      return callback(pointer);
    } finally {
      this.#module._free(pointer);
    }
  }

  #call(name, ...args) {
    const result = this.#module[name](...args);
    if (result !== 0) throw new Error(`${name} failed with simulator result ${result}`);
  }

  #withCString(value, callback) {
    const bytes = encoder.encode(String(value));
    return this.#allocation(bytes.length + 1, (pointer) => {
      this.#module.HEAPU8.set(bytes, pointer);
      this.#module.HEAPU8[pointer + bytes.length] = 0;
      return callback(pointer);
    });
  }

  reset(at) { this.#call('_simulator_reset', timestamp(at)); }
  tick(at) { this.#call('_simulator_tick', timestamp(at)); }
  buttonA(at) { this.#call('_simulator_button_a', timestamp(at)); }
  buttonBDown(at) { this.#call('_simulator_button_b_down', timestamp(at)); }
  buttonBUp(at) { this.#call('_simulator_button_b_up', timestamp(at)); }
  setConnection(connection, at) {
    this.#call('_simulator_set_connection', Number(connection), timestamp(at));
  }
  setBattery(percent, at) {
    this.#call('_simulator_set_battery', Number(percent), timestamp(at));
  }
  setPreviewsHidden(hidden) {
    this.#call('_simulator_set_previews_hidden', hidden ? 1 : 0);
  }
  sendNotification({ title = '', subtitle = '', message = '' }, at) {
    return this.#withCString(title, (titlePointer) =>
      this.#withCString(subtitle, (subtitlePointer) =>
        this.#withCString(message, (messagePointer) =>
          this.#call('_simulator_send_notification', titlePointer, subtitlePointer,
            messagePointer, timestamp(at)))));
  }
  takeBeep() {
    const result = this.#module._simulator_take_beep();
    if (result < 0) throw new Error(`_simulator_take_beep failed with ${result}`);
    return result === 1;
  }
  snapshot() {
    return this.#allocation(SNAPSHOT_SIZE, (pointer) => {
      this.#call('_simulator_snapshot', pointer);
      const bytes = Uint8Array.from(this.#module.HEAPU8.subarray(pointer, pointer + SNAPSHOT_SIZE));
      const snapshot = decodeSnapshot(bytes);
      if (snapshot.abiVersion !== SUPPORTED_ABI_VERSION) {
        throw new Error(`snapshot ABI changed to ${snapshot.abiVersion}`);
      }
      return snapshot;
    });
  }
  displayPlan() {
    return this.#allocation(DISPLAY_PLAN_SIZE, (pointer) => {
      this.#call('_simulator_display_plan', pointer);
      const bytes = Uint8Array.from(
        this.#module.HEAPU8.subarray(pointer, pointer + DISPLAY_PLAN_SIZE));
      return decodePlan(bytes);
    });
  }
  fontGlyph(codepoint) {
    if (this.#fontCache.has(codepoint)) return this.#fontCache.get(codepoint);
    const glyph = this.#allocation(FONT_GLYPH_SIZE, (pointer) => {
      this.#call('_simulator_font_glyph', Number(codepoint), pointer);
      const bytes = Uint8Array.from(
        this.#module.HEAPU8.subarray(pointer, pointer + FONT_GLYPH_SIZE));
      const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
      return new Uint16Array(Array.from({ length: 20 }, (_, row) =>
        view.getUint16(row * 2, true)));
    });
    this.#fontCache.set(codepoint, glyph);
    return glyph;
  }
}

export async function loadWasmClient(base = import.meta.url) {
  const urls = wasmAssetUrls(base);
  const createModule = (await import(urls.module.href)).default;
  const module = await createModule({
    locateFile: (name) => name.endsWith('.wasm') ? urls.wasm.href : new URL(name, urls.module).href,
  });
  return new WasmClient(module);
}
