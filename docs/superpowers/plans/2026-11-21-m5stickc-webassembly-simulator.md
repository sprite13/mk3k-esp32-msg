# M5StickC WebAssembly Simulator Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and publish a mobile-first WebAssembly simulator of the M5StickC Plus2 Telegram notification firmware at `https://les13.tech/mk3k-esp32-preview`.

**Architecture:** Move the pure reducer from `main/` into an ESP-IDF-independent `app_core` component, then compile that component plus the existing notification and display-layout code both natively and with pinned Emscripten 4.0.18. A thin C ABI exposes snapshots to JavaScript; the browser owns only input, Canvas rendering, audio and page layout. Deployment is static, path-prefix-safe, reversible and isolated to one nginx location.

**Tech Stack:** C17, ESP-IDF v6.1.1, Emscripten 4.0.18, CMake/CTest, ASan/UBSan, HTML5 Canvas, vanilla ES modules, Web Audio API, nginx.

---

## File map

- `components/app_core/` — platform-independent state, reducer and screen model.
- `main/app_main.c` — ESP-IDF adapter and hardware side effects only.
- `components/display_ui/display_layout.*` — shared text/layout and bitmap glyph source.
- `simulator/wasm/` — stable C ABI and Emscripten target.
- `simulator/web/` — static mobile UI, Canvas renderer, scenarios and browser tests.
- `scripts/build-web-simulator.sh` — pinned reproducible web build.
- `scripts/deploy-web-simulator.sh` — release-directory deployment and rollback on `les13.tech`.
- `deploy/nginx/mk3k-esp32-preview.conf` — exact path-prefix nginx location.
- `tests/host/` — native reducer and ABI tests.

### Task 1: Extract platform-independent `app_core`

**Files:**
- Create: `components/app_core/CMakeLists.txt`
- Create: `components/app_core/include/app_state.h`
- Create: `components/app_core/app_state.c`
- Create: `components/app_core/include/app_screen.h`
- Create: `components/app_core/app_screen.c`
- Modify: `main/CMakeLists.txt`
- Modify: `main/app_main.c`
- Delete: `main/app_state.c`
- Delete: `main/app_state.h`
- Modify: `tests/host/CMakeLists.txt`
- Modify: `tests/host/test_app_state.c`
- Create: `tests/host/test_app_screen.c`

- [ ] **Step 1: Add failing tests for a pure screen model and battery input**

Extend `test_app_state.c` so battery percentage arrives as an event and add `test_app_screen.c` asserting the current queue selection, connection, battery, reset state and progress are copied into a platform-neutral snapshot:

```c
app_state_t state;
app_state_init(&state);
app_state_reduce(&state, &(app_event_t){
    .type = APP_EVENT_BATTERY_PERCENT,
    .data.battery_percent = 14U,
}, 0);

app_screen_t screen;
app_screen_make(&state, &screen);
CHECK(screen.battery_percent == 14U);
CHECK(screen.connection == APP_CONNECTION_PAIRING);
CHECK(screen.total == 0U);
CHECK(screen.notification_present == false);
```

- [ ] **Step 2: Run the tests and verify RED**

Run:

```bash
rm -rf /tmp/m5-wasm-host
cmake -S tests/host -B /tmp/m5-wasm-host \
  -DCMAKE_C_FLAGS="-Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build /tmp/m5-wasm-host
```

Expected: compilation fails because `APP_EVENT_BATTERY_PERCENT`, `app_screen_t` and `app_screen_make()` do not exist.

- [ ] **Step 3: Move the reducer and add the screen model**

Keep existing reducer behavior and constants. Remove the `power_manager.h` dependency. Add:

```c
typedef struct {
    app_connection_state_t connection;
    uint8_t battery_percent;
    size_t position;
    size_t total;
    bool notification_present;
    notification_t notification;
    app_bond_reset_state_t bond_reset_state;
    uint8_t bond_reset_progress_percent;
    bool display_awake;
} app_screen_t;

void app_screen_make(const app_state_t *state, app_screen_t *screen);
```

`app_state_t` stores `battery_percent`; `APP_BATTERY_PERCENT_UNKNOWN` remains `UINT8_MAX`. `app_screen_make()` copies the selected notification rather than exposing a queue pointer.

Register the ESP-IDF component:

```cmake
idf_component_register(
    SRCS "app_state.c" "app_screen.c"
    INCLUDE_DIRS "include"
    REQUIRES notification_core
)
project_apply_compile_policy(${COMPONENT_LIB})
```

Update `main/app_main.c` to convert `board_battery_millivolts()` with `power_battery_percent()` and emit `APP_EVENT_BATTERY_PERCENT` before rendering. Map `app_screen_t` into `display_ui_model_t` in the hardware adapter.

- [ ] **Step 4: Verify host and firmware builds**

Run:

```bash
cmake --build /tmp/m5-wasm-host
ctest --test-dir /tmp/m5-wasm-host --output-on-failure
source /home/clawd/esp/esp-idf-v6.1.1/export.sh
idf.py fullclean
idf.py build
```

Expected: all host tests pass and ESP-IDF build exits 0.

- [ ] **Step 5: Commit**

```bash
git add components/app_core main tests/host
git commit -m "refactor: extract platform-independent app core"
```

### Task 2: Add a stable simulator C ABI

**Files:**
- Create: `simulator/wasm/simulator_api.h`
- Create: `simulator/wasm/simulator_api.c`
- Create: `simulator/wasm/CMakeLists.txt`
- Create: `tests/host/test_simulator_api.c`
- Modify: `tests/host/CMakeLists.txt`

- [ ] **Step 1: Write failing lifecycle and snapshot tests**

Test reset, notification injection, button navigation, connection, battery and timer progression through the public ABI only:

```c
simulator_reset(0);
CHECK(simulator_abi_version() == SIMULATOR_ABI_VERSION);
CHECK(simulator_set_connection(SIM_CONNECTION_CONNECTED, 10) == SIM_OK);
CHECK(simulator_send_notification("Алиса", "", "Привет", 20) == SIM_OK);

simulator_snapshot_t snapshot;
CHECK(simulator_snapshot(&snapshot) == SIM_OK);
CHECK(snapshot.total == 1U);
CHECK(snapshot.position == 1U);
CHECK(snapshot.display_awake == 1U);
CHECK(strcmp(snapshot.notification.title, "Алиса") == 0);

CHECK(simulator_tick(INT64_C(10000020)) == SIM_OK);
CHECK(simulator_snapshot(&snapshot) == SIM_OK);
CHECK(snapshot.display_awake == 0U);
```

Also test null pointers, overlong UTF-8 input, disconnected notification rejection, six-message eviction and A/B ordering.

- [ ] **Step 2: Verify RED**

Run the host configure/build command from Task 1.

Expected: compilation fails because `simulator_api.h` and functions do not exist.

- [ ] **Step 3: Implement the fixed ABI**

Use a single private simulator instance. Define explicit-width ABI fields and no exposed internal pointers:

```c
#define SIMULATOR_ABI_VERSION 1U

typedef struct {
    uint32_t abi_version;
    uint8_t connection;
    uint8_t battery_percent;
    uint8_t position;
    uint8_t total;
    uint8_t display_awake;
    uint8_t notification_present;
    uint8_t beep_requested;
    uint8_t reserved;
    notification_t notification;
} simulator_snapshot_t;
```

Export `simulator_reset`, `simulator_tick`, `simulator_send_notification`, `simulator_button_a`, `simulator_button_b_down`, `simulator_button_b_up`, `simulator_set_connection`, `simulator_set_battery`, `simulator_set_previews_hidden`, `simulator_take_beep` and `simulator_snapshot` with `EMSCRIPTEN_KEEPALIVE` behind `#ifdef __EMSCRIPTEN__`.

Use the Telegram bundle ID `ph.telegra.Telegraph` inside injected notifications. When previews are hidden, clear title/subtitle/message before entering `app_core`. Reject injection while disconnected with `SIM_ERR_DISCONNECTED`.

- [ ] **Step 4: Verify native ABI tests**

Run:

```bash
cmake --build /tmp/m5-wasm-host
ctest --test-dir /tmp/m5-wasm-host --output-on-failure
```

Expected: all tests pass under ASan/UBSan.

- [ ] **Step 5: Commit**

```bash
git add simulator/wasm tests/host
git commit -m "feat: add WebAssembly simulator ABI"
```

### Task 3: Pin Emscripten and prove native/WASM parity

**Files:**
- Create: `.emscripten-version`
- Create: `scripts/install-emsdk.sh`
- Create: `scripts/build-web-simulator.sh`
- Create: `simulator/wasm/exports.json`
- Create: `simulator/wasm/native_scenarios.c`
- Create: `simulator/web/tests/parity.mjs`
- Create: `simulator/web/tests/scenarios.mjs`
- Modify: `.gitignore`
- Modify: `README.md`

- [ ] **Step 1: Add scenarios that fail without a WASM build**

Define deterministic scenarios using timestamps in microseconds:

```js
export const scenarios = [
  { name: 'empty', actions: [{ type: 'reset', at: 0 }] },
  { name: 'one-message', actions: [
    { type: 'reset', at: 0 },
    { type: 'connect', at: 10 },
    { type: 'notify', at: 20, title: 'Алиса', subtitle: '', message: 'Привет' },
  ]},
  { name: 'six-evicts-oldest', actions: [
    { type: 'reset', at: 0 },
    { type: 'connect', at: 10 },
    { type: 'notify', at: 20, title: '1', subtitle: '', message: 'one' },
    { type: 'notify', at: 30, title: '2', subtitle: '', message: 'two' },
    { type: 'notify', at: 40, title: '3', subtitle: '', message: 'three' },
    { type: 'notify', at: 50, title: '4', subtitle: '', message: 'four' },
    { type: 'notify', at: 60, title: '5', subtitle: '', message: 'five' },
    { type: 'notify', at: 70, title: '6', subtitle: '', message: 'six' },
  ]},
  { name: 'screen-timeout', actions: [
    { type: 'reset', at: 0 },
    { type: 'connect', at: 10 },
    { type: 'notify', at: 20, title: 'Алиса', subtitle: '', message: 'Привет' },
    { type: 'tick', at: 10_000_020 },
  ]},
];
```

`native_scenarios.c` runs this same canonical action list through `simulator_api.c` and writes one normalized JSON snapshot per scenario. `build-web-simulator.sh` stores its output as `artifacts/native-scenarios.json`. `parity.mjs` loads `dist/mk3k-simulator.mjs`, replays the same actions in WASM and deep-compares every normalized snapshot with that file.

- [ ] **Step 2: Verify RED**

Run:

```bash
node simulator/web/tests/parity.mjs
```

Expected: failure because `dist/mk3k-simulator.mjs` does not exist.

- [ ] **Step 3: Pin and build with Emscripten 4.0.18**

`.emscripten-version` contains exactly `4.0.18`. `install-emsdk.sh` clones tag `4.0.18` to a task-specific cache, runs `emsdk install 4.0.18` and `emsdk activate 4.0.18`, and never overwrites unrelated SDKs.

`build-web-simulator.sh` sources `emsdk_env.sh`, compiles C17 with `-Wall -Wextra -Werror`, enables modular ES output and exports only the ABI:

```bash
emcc \
  components/app_core/app_state.c \
  components/app_core/app_screen.c \
  components/notification_core/notification_queue.c \
  components/notification_core/notification_normalizer.c \
  simulator/wasm/simulator_api.c \
  -std=c17 -O2 -Wall -Wextra -Werror \
  -Icomponents/app_core/include \
  -Icomponents/notification_core/include \
  -Isimulator/wasm \
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sALLOW_MEMORY_GROWTH=0 \
  -sEXPORTED_FUNCTIONS=@simulator/wasm/exports.json \
  -sEXPORTED_RUNTIME_METHODS='["cwrap","UTF8ToString"]' \
  -o simulator/web/dist/mk3k-simulator.mjs
```

Copy the exact `.wasm` filename emitted by Emscripten into `simulator/web/dist/`; do not inline it into JavaScript.

- [ ] **Step 4: Run parity and native tests**

Run:

```bash
scripts/build-web-simulator.sh
node simulator/web/tests/parity.mjs
ctest --test-dir /tmp/m5-wasm-host --output-on-failure
```

Expected: every named scenario reports identical native and WASM snapshots; host tests pass.

- [ ] **Step 5: Commit**

```bash
git add .emscripten-version .gitignore scripts simulator README.md
git commit -m "build: add pinned WebAssembly toolchain and parity tests"
```

### Task 4: Build the mobile-first simulator UI

**Files:**
- Create: `simulator/web/index.html`
- Create: `simulator/web/styles/main.css`
- Create: `simulator/web/src/app.mjs`
- Create: `simulator/web/src/wasm-client.mjs`
- Create: `simulator/web/src/device-renderer.mjs`
- Create: `simulator/web/src/scenarios.mjs`
- Create: `simulator/web/src/audio.mjs`
- Create: `simulator/web/assets/m5stick-outline.svg`
- Create: `simulator/web/tests/dom-smoke.mjs`

- [ ] **Step 1: Add failing DOM and renderer smoke tests**

Using Node's built-in test runner and a minimal fake Canvas context, assert:

```js
test('renders a 135x240 logical screen and controls', async () => {
  const html = await readFile(new URL('../index.html', import.meta.url), 'utf8');
  assert.match(html, /width="135" height="240"/);
  assert.match(html, /id="button-a"/);
  assert.match(html, /id="button-b"/);
  assert.match(html, /id="notification-form"/);
  assert.match(html, /name="title"/);
  assert.match(html, /name="subtitle"/);
  assert.match(html, /name="message"/);
});
```

Test bitmap-glyph plotting, screen-off black frame, low-battery warning palette and path-prefix-safe module/WASM URLs.

- [ ] **Step 2: Verify RED**

Run:

```bash
node --test simulator/web/tests/dom-smoke.mjs
```

Expected: failure because UI files do not exist.

- [ ] **Step 3: Implement semantic HTML and responsive CSS**

The page contains:

```html
<main class="simulator-shell">
  <section class="device-panel" aria-label="Виртуальный M5StickC Plus2">
    <canvas id="device-screen" width="135" height="240"></canvas>
    <button id="button-a" type="button">A</button>
    <button id="button-b" type="button">B</button>
  </section>
  <section class="controls-panel">
    <form id="notification-form">
      <label>Имя <input name="title" maxlength="95"></label>
      <label>Подзаголовок <input name="subtitle" maxlength="95"></label>
      <label>Текст <textarea name="message" maxlength="255"></textarea></label>
      <button type="submit">Отправить</button>
    </form>
  </section>
</main>
```

Use CSS custom properties, minimum 44px touch targets, portrait single-column layout below 760px, two columns above it, visible focus states and no external fonts/CDNs.

- [ ] **Step 4: Implement the WASM client and Canvas renderer**

`wasm-client.mjs` loads assets relative to `import.meta.url`, wraps C exports and copies snapshot data immediately. `device-renderer.mjs` consumes the same render-plan rules and bitmap glyph rows from the build output; it does not format notification text independently.

Use pointer events so B receives distinct down/up events. Run `client.tick(Math.trunc(performance.now() * 1000))` every 100 ms and request a frame only when the snapshot changes.

Form submit calls `sendNotification`. Scenario buttons execute the approved eight scenarios. Connection, battery and hidden-preview controls call the ABI. Audio remains muted until the user enables it, then uses a short oscillator tone only when `takeBeep()` returns true.

- [ ] **Step 5: Verify UI tests and serve under the target prefix**

Run:

```bash
node --test simulator/web/tests/dom-smoke.mjs
python3 -m http.server 8088 --directory simulator/web
curl -fsS http://127.0.0.1:8088/index.html >/dev/null
curl -fsS http://127.0.0.1:8088/dist/mk3k-simulator.wasm >/dev/null
```

Expected: tests pass and both HTTP requests return 200. Stop the temporary server after verification.

- [ ] **Step 6: Commit**

```bash
git add simulator/web
git commit -m "feat: add mobile WebAssembly M5Stick simulator"
```

### Task 5: Add release packaging, nginx configuration and rollback

**Files:**
- Create: `scripts/package-web-simulator.sh`
- Create: `scripts/deploy-web-simulator.sh`
- Create: `deploy/nginx/mk3k-esp32-preview.conf`
- Create: `docs/web-simulator-deployment.md`
- Modify: `README.md`

- [ ] **Step 1: Add failing package validation**

`package-web-simulator.sh --check` must fail unless the release contains `index.html`, `.mjs`, `.wasm`, CSS, no absolute `/dist/...` URLs, and no source maps. Run before implementation:

```bash
scripts/package-web-simulator.sh --check
```

Expected: non-zero because the packaging script does not exist.

- [ ] **Step 2: Implement deterministic packaging**

Build into `artifacts/mk3k-esp32-preview/` with normalized file permissions and a SHA-256 manifest. The release contains only runtime files. Add both:

```html
<meta name="robots" content="noindex,nofollow">
```

and nginx headers:

```nginx
location = /mk3k-esp32-preview {
    return 301 /mk3k-esp32-preview/;
}
location ^~ /mk3k-esp32-preview/ {
    alias /var/www/mk3k-esp32-preview/current/;
    index index.html;
    autoindex off;
    add_header X-Robots-Tag "noindex, nofollow" always;
    add_header Cache-Control "no-cache" always;
    types { application/wasm wasm; }
}
```

The final nginx fragment may be adapted to the existing site only after reading it; it must not replace unrelated locations.

- [ ] **Step 3: Implement atomic deployment and rollback**

The deploy script accepts `--host`, `--release` and `--rollback`. It uploads to `/var/www/mk3k-esp32-preview/releases/<timestamp>-<sha>/`, validates manifest, changes the `current` symlink atomically, runs `sudo nginx -t`, reloads nginx only after success and restores the previous symlink on failure. It never deletes old releases automatically.

- [ ] **Step 4: Verify locally with a temporary nginx/container or config parser**

Run package checks and, if nginx is installed locally, syntax-check a temporary complete config. At minimum:

```bash
scripts/package-web-simulator.sh
scripts/package-web-simulator.sh --check artifacts/mk3k-esp32-preview
find artifacts/mk3k-esp32-preview -type f -maxdepth 3 -print
sha256sum -c artifacts/mk3k-esp32-preview/SHA256SUMS
```

Expected: checks pass and manifest verifies every runtime file.

- [ ] **Step 5: Commit**

```bash
git add scripts deploy docs README.md
git commit -m "ops: add reversible web simulator deployment"
```

### Task 6: Deploy and externally verify the preview

**Files:**
- Modify only if required after inspection: remote nginx site for `les13.tech`
- Update: `docs/web-simulator-deployment.md`

- [ ] **Step 1: Read-only preflight on `les13.tech`**

Using `clawd@les13.tech`, inspect the active nginx site, document root, ownership, disk space and existing `/mk3k-esp32*` routes. Back up the exact site file before any edit. Do not expose credentials or environment files.

Run remotely:

```bash
hostname
nginx -T
df -h /var/www
sudo test -e /var/www/mk3k-esp32-preview && sudo find /var/www/mk3k-esp32-preview -maxdepth 2 -type l -o -type d
```

- [ ] **Step 2: Deploy release and add the isolated location**

Upload the packaged release with `scripts/deploy-web-simulator.sh`. Merge only the reviewed location into the active `les13.tech` server block, run `sudo nginx -t`, then reload.

- [ ] **Step 3: Verify externally**

Run from the local host:

```bash
curl -fsSI https://les13.tech/mk3k-esp32-preview/
curl -fsSI https://les13.tech/mk3k-esp32-preview/dist/mk3k-simulator.wasm
curl -fsS https://les13.tech/mk3k-esp32-preview/ | rg 'noindex|device-screen|notification-form'
```

Expected: HTTPS 200, `Content-Type: application/wasm`, `X-Robots-Tag: noindex, nofollow`, and HTML markers present.

Use a browser automation smoke test at the public URL to submit one Cyrillic notification, click A/B, trigger six-message scenario, toggle connection and wait for screen timeout. Record screenshots for mobile and desktop widths.

- [ ] **Step 4: Exercise rollback and restore the new release**

Deploy a harmless second release marker, run `--rollback`, prove the `current` symlink and public response return to the prior release, then redeploy the approved release. This is required evidence that rollback is operational rather than documentary.

- [ ] **Step 5: Final verification**

Run fresh:

```bash
rm -rf /tmp/m5-wasm-host
cmake -S tests/host -B /tmp/m5-wasm-host \
  -DCMAKE_C_FLAGS="-Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build /tmp/m5-wasm-host
ctest --test-dir /tmp/m5-wasm-host --output-on-failure
scripts/build-web-simulator.sh
node simulator/web/tests/parity.mjs
node --test simulator/web/tests/dom-smoke.mjs
source /home/clawd/esp/esp-idf-v6.1.1/export.sh
idf.py fullclean
idf.py build
git diff --check
```

Expected: all tests pass, WASM/native parity passes, firmware builds and no whitespace errors remain.

- [ ] **Step 6: Commit deployment evidence**

Update deployment documentation with the release identifier, verification timestamp, public URL and rollback result, without server secrets:

```bash
git add docs/web-simulator-deployment.md
git commit -m "docs: record web simulator deployment verification"
```

### Task 7: Whole-branch review

**Files:** all changes since commit `5529308`.

- [ ] **Step 1: Spec compliance review**

Review every requirement in `docs/superpowers/specs/2026-11-21-m5stickc-webassembly-simulator-design.md` against the implementation and public preview. Reject missing behavior and unrequested scope.

- [ ] **Step 2: Code quality and security review**

Review C/WASM memory boundaries, ABI versioning, timer monotonicity, pointer events, audio permission, asset prefixing, nginx isolation, deployment atomicity and rollback safety.

- [ ] **Step 3: Fix and re-review until approved**

Every Critical or Important finding gets a regression test, implementation fix, fresh verification and reviewer re-approval.

- [ ] **Step 4: Hand off the URL**

Report the public preview URL, what is simulated, what still requires physical M5Stick/iPhone validation, and the exact release commit.
