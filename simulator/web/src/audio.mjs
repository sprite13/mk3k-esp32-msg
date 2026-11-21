export class BeepAudio {
  #enabled = false;
  #context = null;
  #AudioContextClass;

  constructor({ AudioContextClass = globalThis.AudioContext ?? globalThis.webkitAudioContext } = {}) {
    this.#AudioContextClass = AudioContextClass;
  }

  get enabled() { return this.#enabled; }

  async setEnabled(enabled) {
    if (!enabled) {
      this.#enabled = false;
      return;
    }
    if (!this.#AudioContextClass) throw new Error('Web Audio API недоступен');
    try {
      this.#context ??= new this.#AudioContextClass();
      await this.#context.resume();
      this.#enabled = true;
    } catch (error) {
      this.#enabled = false;
      const context = this.#context;
      this.#context = null;
      if (context) {
        try { await context.close(); } catch { /* original error is authoritative */ }
      }
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
    this.#enabled = false;
    if (this.#context) {
      const context = this.#context;
      this.#context = null;
      await context.close();
    }
  }
}
