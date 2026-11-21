export const UNKNOWN_BATTERY_PERCENT = 255;

export function readBatteryControl(range, unknown) {
  if (unknown.checked) return UNKNOWN_BATTERY_PERCENT;
  const value = Number(range.value);
  if (!Number.isInteger(value) || value < 0 || value > 100) {
    throw new RangeError('battery percentage must be an integer from 0 to 100');
  }
  return value;
}

export function updateBatteryPresentation(range, unknown, output) {
  range.disabled = unknown.checked;
  output.value = unknown.checked ? 'неизвестно' : `${readBatteryControl(range, unknown)}%`;
  output.textContent = output.value;
}
