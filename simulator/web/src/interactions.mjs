export function bindDeviceButtons({ buttonA, buttonB, onA, onBDown, onBUp, signal }) {
  const options = signal === undefined ? undefined : { signal };
  buttonA.addEventListener('click', onA, options);
  let bIsDown = false;
  const press = (event) => {
    if (bIsDown) return;
    bIsDown = true;
    buttonB.classList.add('is-pressed');
    if (event.pointerId !== undefined && buttonB.setPointerCapture) {
      buttonB.setPointerCapture(event.pointerId);
    }
    onBDown();
  };
  const release = () => {
    if (!bIsDown) return;
    bIsDown = false;
    buttonB.classList.remove('is-pressed');
    onBUp();
  };
  buttonB.addEventListener('pointerdown', press, options);
  buttonB.addEventListener('pointerup', release, options);
  buttonB.addEventListener('pointercancel', release, options);
  buttonB.addEventListener('lostpointercapture', release, options);
  buttonB.addEventListener('keydown', (event) => {
    if ((event.key === ' ' || event.key === 'Enter') && !event.repeat) press(event);
  }, options);
  buttonB.addEventListener('keyup', (event) => {
    if (event.key === ' ' || event.key === 'Enter') release();
  }, options);
}
