import { BeepAudio } from './audio.mjs';
import { readBatteryControl, updateBatteryPresentation } from './controls.mjs';
import { renderDeviceScreen } from './device-renderer.mjs';
import { createFrameScheduler } from './frame-scheduler.mjs';
import { bindDeviceButtons } from './interactions.mjs';
import { UI_SCENARIOS } from './scenarios.mjs';
import { createStartupController } from './startup-controller.mjs';
import { loadWasmClient } from './wasm-client.mjs';

const nowUs = () => BigInt(Math.trunc(performance.now() * 1000));
const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));
const element = (selector) => document.querySelector(selector);

async function startRuntime(signal) {
  const canvas = element('#device-screen');
  const form = element('#notification-form');
  const connection = element('#connection');
  const battery = element('#battery');
  const batteryUnknown = element('#battery-unknown');
  const batteryValue = element('#battery-value');
  const previews = element('#previews-hidden');
  const audioEnabled = element('#audio-enabled');
  const status = element('#runtime-status');
  const reset = element('#reset-simulator');
  const client = await loadWasmClient();
  if (signal.aborted) return;

  const audio = new BeepAudio();
  const listenerOptions = { signal };
  let lastFrame = '';
  const frames = createFrameScheduler(requestAnimationFrame, ({ snapshot, plan }) =>
    renderDeviceScreen(canvas, snapshot, plan, (codepoint) => client.fontGlyph(codepoint)));
  signal.addEventListener('abort', () => {
    frames.stop();
    void audio.dispose().catch(() => {});
  }, { once: true });

  const setStatus = (message, error = false) => {
    status.textContent = message;
    status.dataset.error = error ? 'true' : 'false';
  };
  const perform = (operation) => {
    try {
      operation();
      return true;
    } catch (error) {
      setStatus(error.message, true);
      return false;
    }
  };
  const refresh = () => {
    if (client.takeBeep()) audio.play();
    const snapshot = client.snapshot();
    const plan = client.displayPlan();
    const signature = JSON.stringify([snapshot, plan]);
    if (signature !== lastFrame) {
      frames.schedule({ snapshot, plan });
      lastFrame = signature;
    }
    return snapshot;
  };
  const applyBattery = () => {
    updateBatteryPresentation(battery, batteryUnknown, batteryValue);
    client.setBattery(readBatteryControl(battery, batteryUnknown), nowUs());
  };
  const resetState = () => {
    const at = nowUs();
    client.reset(at);
    client.setConnection(Number(connection.value), at);
    client.setBattery(readBatteryControl(battery, batteryUnknown), at);
    client.setPreviewsHidden(previews.checked);
    lastFrame = '';
    refresh();
    setStatus('Симулятор готов');
  };

  bindDeviceButtons({
    buttonA: element('#button-a'),
    buttonB: element('#button-b'),
    signal,
    onA: () => perform(() => { client.buttonA(nowUs()); refresh(); }),
    onBDown: () => perform(() => { client.buttonBDown(nowUs()); refresh(); }),
    onBUp: () => perform(() => { client.buttonBUp(nowUs()); refresh(); }),
  });
  form.addEventListener('submit', (event) => {
    event.preventDefault();
    const data = new FormData(form);
    if (perform(() => {
      client.sendNotification({
        title: data.get('title'), subtitle: data.get('subtitle'), message: data.get('message'),
      }, nowUs());
      refresh();
    })) setStatus('Уведомление добавлено');
  }, listenerOptions);
  connection.addEventListener('change', () => perform(() => {
    client.setConnection(Number(connection.value), nowUs());
    refresh();
  }), listenerOptions);
  battery.addEventListener('input', () => perform(() => {
    updateBatteryPresentation(battery, batteryUnknown, batteryValue);
    if (!batteryUnknown.checked) {
      client.setBattery(readBatteryControl(battery, batteryUnknown), nowUs());
      refresh();
    }
  }), listenerOptions);
  batteryUnknown.addEventListener('change', () => perform(() => {
    applyBattery();
    refresh();
  }), listenerOptions);
  previews.addEventListener('change', () => perform(() =>
    client.setPreviewsHidden(previews.checked)), listenerOptions);
  audioEnabled.addEventListener('change', async () => {
    try {
      await audio.setEnabled(audioEnabled.checked);
      setStatus(audioEnabled.checked ? 'Звук включён' : 'Звук выключен');
    } catch (error) {
      audioEnabled.checked = false;
      setStatus(error.message, true);
    }
  }, listenerOptions);
  reset.addEventListener('click', () => perform(resetState), listenerOptions);

  for (const button of document.querySelectorAll('[data-scenario]')) {
    button.addEventListener('click', async () => {
      const scenario = UI_SCENARIOS[button.dataset.scenario];
      if (!scenario) return;
      button.disabled = true;
      try {
        client.reset(nowUs());
        client.setBattery(readBatteryControl(battery, batteryUnknown), nowUs());
        for (const action of scenario.actions) {
          if (signal.aborted) return;
          if (action.type === 'wait') {
            await sleep(action.milliseconds);
          } else if (action.type === 'connect') {
            connection.value = String(action.connection);
            client.setConnection(action.connection, nowUs());
          } else if (action.type === 'previews') {
            previews.checked = action.hidden;
            client.setPreviewsHidden(action.hidden);
          } else if (action.type === 'notify') {
            client.sendNotification(action, nowUs());
          }
          refresh();
        }
        setStatus(`Сценарий: ${scenario.label}`);
      } catch (error) {
        setStatus(error.message, true);
      } finally {
        button.disabled = false;
      }
    }, listenerOptions);
  }

  updateBatteryPresentation(battery, batteryUnknown, batteryValue);
  resetState();
  const timer = setInterval(() => perform(() => {
    client.tick(nowUs());
    refresh();
  }), 100);
  signal.addEventListener('abort', () => clearInterval(timer), { once: true });
}

const status = element('#runtime-status');
const retry = element('#retry-start');
const controller = createStartupController({
  start: startRuntime,
  onState(state, error) {
    retry.hidden = state !== 'failed';
    retry.disabled = state !== 'failed';
    status.dataset.error = state === 'failed' ? 'true' : 'false';
    if (state === 'loading') status.textContent = 'Загрузка WebAssembly…';
    if (state === 'running') status.textContent = 'Симулятор готов';
    if (state === 'failed') {
      status.textContent = `Не удалось запустить симулятор: ${error.message}. Проверьте сеть и повторите загрузку.`;
    }
  },
});

retry.addEventListener('click', () => void controller.attempt());
void controller.attempt();
