function abortError() {
  const error = new Error('scenario aborted');
  error.name = 'AbortError';
  return error;
}

export function abortableDelay(milliseconds, signal) {
  if (signal.aborted) return Promise.reject(abortError());
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      signal.removeEventListener('abort', abort);
      resolve();
    }, milliseconds);
    const abort = () => {
      clearTimeout(timer);
      reject(abortError());
    };
    signal.addEventListener('abort', abort, { once: true });
  });
}

export function createScenarioController({ run, onActive }) {
  let generation = 0;
  let activeAbortController = null;
  return Object.freeze({
    async start(scenario) {
      const ownGeneration = ++generation;
      activeAbortController?.abort();
      const abortController = new AbortController();
      activeAbortController = abortController;
      onActive(true);
      try {
        await run(scenario, abortController.signal);
        return ownGeneration === generation && !abortController.signal.aborted;
      } catch (error) {
        if (abortController.signal.aborted || error?.name === 'AbortError') return false;
        throw error;
      } finally {
        if (ownGeneration === generation) {
          activeAbortController = null;
          onActive(false);
        }
      }
    },
    cancel() {
      generation += 1;
      activeAbortController?.abort();
      activeAbortController = null;
      onActive(false);
    },
  });
}
