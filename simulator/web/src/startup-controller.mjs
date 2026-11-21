export function createStartupController({ start, onState }) {
  let generation = 0;
  let activeAbortController = null;
  return Object.freeze({
    async attempt() {
      const ownGeneration = ++generation;
      activeAbortController?.abort();
      const abortController = new AbortController();
      activeAbortController = abortController;
      onState('loading');
      try {
        await start(abortController.signal);
        if (ownGeneration !== generation || abortController.signal.aborted) return false;
        onState('running');
        return true;
      } catch (error) {
        if (ownGeneration !== generation) return false;
        abortController.abort();
        onState('failed', error instanceof Error ? error : new Error(String(error)));
        return false;
      }
    },
    stop() {
      generation += 1;
      activeAbortController?.abort();
      activeAbortController = null;
    },
  });
}
