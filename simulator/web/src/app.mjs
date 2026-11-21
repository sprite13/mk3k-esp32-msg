import { BeepAudio } from './audio.mjs';
import { renderDeviceScreen } from './device-renderer.mjs';
import { bindDeviceButtons } from './interactions.mjs';
import { UI_SCENARIOS } from './scenarios.mjs';
import { loadWasmClient } from './wasm-client.mjs';

const nowUs = () => BigInt(Math.trunc(performance.now() * 1000));
const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

async function start() {
  const canvas = document.querySelector('#device-screen');
  const form = document.querySelector('#notification-form');
  const connection = document.querySelector('#connection');
  const battery = document.querySelector('#battery');
  const previews = document.querySelector('#previews-hidden');
  const audioEnabled = document.querySelector('#audio-enabled');
  const status = document.querySelector('#runtime-status');
  const reset = document.querySelector('#reset-simulator');
  const client = await loadWasmClient();
  const audio = new BeepAudio();
  let lastFrame = '';

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
      renderDeviceScreen(canvas, snapshot, plan, (codepoint) => client.fontGlyph(codepoint));
      lastFrame = signature;
    }
    return snapshot;
  };
  const resetState = () => {
    const at = nowUs();
    client.reset(at);
    client.setConnection(Number(connection.value), at);
    client.setBattery(Number(battery.value), at);
    client.setPreviewsHidden(previews.checked);
    lastFrame = '';
    refresh();
    setStatus('Симулятор готов');
  };

  bindDeviceButtons({
    buttonA: document.querySelector('#button-a'),
    buttonB: document.querySelector('#button-b'),
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
  });
  connection.addEventListener('change', () => perform(() => {
    client.setConnection(Number(connection.value), nowUs());
    refresh();
  }));
  battery.addEventListener('change', () => perform(() => {
    client.setBattery(Number(battery.value), nowUs());
    refresh();
  }));
  previews.addEventListener('change', () => perform(() =>
    client.setPreviewsHidden(previews.checked)));
  audioEnabled.addEventListener('change', async () => {
    try {
      await audio.setEnabled(audioEnabled.checked);
      setStatus(audioEnabled.checked ? 'Звук включён' : 'Звук выключен');
    } catch (error) {
      audioEnabled.checked = false;
      setStatus(error.message, true);
    }
  });
  reset.addEventListener('click', () => perform(resetState));

  for (const button of document.querySelectorAll('[data-scenario]')) {
    button.addEventListener('click', async () => {
      const scenario = UI_SCENARIOS[button.dataset.scenario];
      if (!scenario) return;
      button.disabled = true;
      try {
        client.reset(nowUs());
        client.setBattery(Number(battery.value), nowUs());
        for (const action of scenario.actions) {
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
    });
  }

  resetState();
  setInterval(() => perform(() => {
    client.tick(nowUs());
    refresh();
  }), 100);
}

start().catch((error) => {
  const status = document.querySelector('#runtime-status');
  status.textContent = `Ошибка запуска: ${error.message}`;
  status.dataset.error = 'true';
});
