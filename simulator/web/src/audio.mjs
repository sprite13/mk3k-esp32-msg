export class BeepAudio {
  #enabled = false;
  #context = null;

  async setEnabled(enabled) {
    this.#enabled = Boolean(enabled);
    if (this.#enabled) {
      const AudioContextClass = globalThis.AudioContext ?? globalThis.webkitAudioContext;
      if (!AudioContextClass) throw new Error('Web Audio API недоступен');
      this.#context ??= new AudioContextClass();
      await this.#context.resume();
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
}
