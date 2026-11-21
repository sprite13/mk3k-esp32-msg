export class BeepAudio {
  #enabled = false;
  #context = null;
  #AudioContextClass;
  #generation = 0;
  #desired = false;

  constructor({ AudioContextClass = globalThis.AudioContext ?? globalThis.webkitAudioContext } = {}) {
    this.#AudioContextClass = AudioContextClass;
  }

  get enabled() { return this.#enabled; }

  async setEnabled(enabled) {
    const generation = ++this.#generation;
    this.#desired = Boolean(enabled);
    if (!enabled) {
      this.#enabled = false;
      const context = this.#context;
      this.#context = null;
      if (context) {
        try { await context.close(); } catch { /* disabling remains authoritative */ }
      }
      return;
    }
    if (!this.#AudioContextClass) throw new Error('Web Audio API недоступен');
    const previous = this.#context;
    const context = new this.#AudioContextClass();
    this.#context = context;
    this.#enabled = false;
    if (previous) {
      try { await previous.close(); } catch { /* replacement remains authoritative */ }
    }
    try {
      await context.resume();
      if (generation !== this.#generation || !this.#desired || this.#context !== context) {
        try { await context.close(); } catch { /* stale contexts are already detached */ }
        return false;
      }
      this.#enabled = true;
      return true;
    } catch (error) {
      const current = generation === this.#generation && this.#context === context;
      if (current) {
        this.#enabled = false;
        this.#desired = false;
        this.#context = null;
      }
      try { await context.close(); } catch { /* resume error is authoritative */ }
      if (!current) return false;
      throw error;
    }
  }

  play() {
    if (!this.#enabled || !this.#context) return;
    const oscillator = this.#context.createOscillator();
    const gain = this.#context.createGain();
    const now = this.#context.currentTime;
    oscillator.frequency.setValueAtTime(2200, now);
    gain.gain.setValueAtTime(0.035, now);
    gain.gain.exponentialRampToValueAtTime(0.001, now + 0.06);
    oscillator.connect(gain).connect(this.#context.destination);
    oscillator.start(now);
    oscillator.stop(now + 0.06);
  }

  async dispose() {
    ++this.#generation;
    this.#desired = false;
    this.#enabled = false;
    if (this.#context) {
      const context = this.#context;
      this.#context = null;
      await context.close();
    }
  }
}
