export function bindDeviceButtons({ buttonA, buttonB, onA, onBDown, onBUp }) {
  buttonA.addEventListener('click', onA);
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
  buttonB.addEventListener('pointerdown', press);
  buttonB.addEventListener('pointerup', release);
  buttonB.addEventListener('pointercancel', release);
  buttonB.addEventListener('lostpointercapture', release);
  buttonB.addEventListener('keydown', (event) => {
    if ((event.key === ' ' || event.key === 'Enter') && !event.repeat) press(event);
  });
  buttonB.addEventListener('keyup', (event) => {
    if (event.key === ' ' || event.key === 'Enter') release();
  });
}
