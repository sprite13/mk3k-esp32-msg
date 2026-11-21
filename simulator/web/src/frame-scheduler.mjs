export function createFrameScheduler(requestFrame, draw) {
  let pending = false;
  let active = true;
  let latest;
  return Object.freeze({
    schedule(frame) {
      if (!active) return;
      latest = frame;
      if (pending) return;
      pending = true;
      requestFrame(() => {
        pending = false;
        if (!active) return;
        const frameToDraw = latest;
        latest = undefined;
        draw(frameToDraw);
      });
    },
    stop() {
      active = false;
      latest = undefined;
    },
  });
}
