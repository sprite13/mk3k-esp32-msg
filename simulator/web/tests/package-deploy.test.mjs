import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, readFileSync, writeFileSync, chmodSync, cpSync, existsSync, lstatSync, linkSync, readlinkSync, readdirSync, rmSync, symlinkSync, unlinkSync, statSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { spawnSync } from 'node:child_process';
import test from 'node:test';

const root = resolve(import.meta.dirname, '../../..');
const packageScript = join(root, 'scripts/package-web-simulator.sh');
const deployScript = join(root, 'scripts/deploy-web-simulator.sh');
const artifact = join(root, 'artifacts/mk3k-esp32-preview');
const nginxFragment = join(root, 'deploy/nginx/mk3k-esp32-preview.conf');

function run(command, args = [], options = {}) {
  const env = { ...process.env, ...options.env };
  delete env.NODE_TEST_CONTEXT;
  return spawnSync(command, args, {
    cwd: root,
    encoding: 'utf8',
    env,
  });
}

function filesBelow(directory, prefix = '') {
  return readdirSync(join(directory, prefix), { withFileTypes: true })
    .flatMap((entry) => {
      const name = join(prefix, entry.name);
      return entry.isDirectory() ? filesBelow(directory, name) : [name];
    })
    .sort();
}

function makeTreeWritable(directory) {
  if (!existsSync(directory)) return;
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) makeTreeWritable(path);
    if (!entry.isSymbolicLink()) chmodSync(path, entry.isDirectory() ? 0o755 : 0o644);
  }
  chmodSync(directory, 0o755);
}

function rewriteManifest(directory) {
  const runtimeFiles = filesBelow(directory).filter((name) => name !== 'SHA256SUMS');
  const manifest = spawnSync('sha256sum', runtimeFiles, { cwd: directory, encoding: 'utf8' });
  assert.equal(manifest.status, 0, manifest.stderr || manifest.stdout);
  writeFileSync(join(directory, 'SHA256SUMS'), manifest.stdout);
}

test('package contains only deterministic runtime files with a complete manifest', () => {
  const result = run(packageScript);
  assert.equal(result.status, 0, result.stderr || result.stdout);
  assert.deepEqual(filesBelow(artifact), [
    'SHA256SUMS',
    'assets/m5stick-outline.svg',
    'dist/mk3k-simulator.mjs',
    'dist/mk3k-simulator.wasm',
    'index.html',
    'src/accessibility.mjs',
    'src/app.mjs',
    'src/audio.mjs',
    'src/controls.mjs',
    'src/device-renderer.mjs',
    'src/frame-scheduler.mjs',
    'src/interactions.mjs',
    'src/scenario-controller.mjs',
    'src/scenarios.mjs',
    'src/startup-controller.mjs',
    'src/wasm-client.mjs',
    'styles/main.css',
  ]);
  const check = run(packageScript, ['--check', artifact]);
  assert.equal(check.status, 0, check.stderr || check.stdout);
  // Run in the artifact directory separately because SHA256SUMS paths are relative.
  const direct = spawnSync('sha256sum', ['-c', 'SHA256SUMS'], { cwd: artifact, encoding: 'utf8' });
  assert.equal(direct.status, 0, direct.stderr || direct.stdout);
  assert.equal(readFileSync(join(artifact, 'SHA256SUMS'), 'utf8').trim().split('\n').length, filesBelow(artifact).length - 1);
});

test('package validation rejects source maps, symlinks and absolute-root URLs', () => {
  const fixture = mkdtempSync(join(tmpdir(), 'mk3k-package-invalid-'));
  try {
    cpSync(artifact, fixture, { recursive: true });
    makeTreeWritable(fixture);
    writeFileSync(join(fixture, 'leak.map'), '{}\n');
    let result = run(packageScript, ['--check', fixture]);
    assert.notEqual(result.status, 0);
    rmSync(join(fixture, 'leak.map'));

    symlinkSync('index.html', join(fixture, 'linked-index'));
    result = run(packageScript, ['--check', fixture]);
    assert.notEqual(result.status, 0);
    unlinkSync(join(fixture, 'linked-index'));

    const pipe = join(fixture, 'unexpected-pipe');
    const fifo = spawnSync('mkfifo', [pipe], { encoding: 'utf8' });
    assert.equal(fifo.status, 0, fifo.stderr || fifo.stdout);
    result = run(packageScript, ['--check', fixture]);
    assert.notEqual(result.status, 0);
    unlinkSync(pipe);

    const cssPath = join(fixture, 'styles/main.css');
    const originalCss = readFileSync(cssPath, 'utf8');
    unlinkSync(cssPath);
    linkSync(join(fixture, 'index.html'), cssPath);
    rewriteManifest(fixture);
    result = run(packageScript, ['--check', fixture]);
    assert.notEqual(result.status, 0);
    unlinkSync(cssPath);
    writeFileSync(cssPath, originalCss);
    chmodSync(cssPath, 0o644);

    const originalHtml = readFileSync(join(fixture, 'index.html'), 'utf8');
    writeFileSync(join(fixture, 'index.html'), originalHtml.replace('./styles/main.css', '//cdn.invalid/main.css'));
    rewriteManifest(fixture);
    result = run(packageScript, ['--check', fixture]);
    assert.notEqual(result.status, 0);

    writeFileSync(join(fixture, 'index.html'), originalHtml.replace('./styles/main.css', '//evil.example'));
    rewriteManifest(fixture);
    result = run(packageScript, ['--check', fixture]);
    assert.notEqual(result.status, 0);

    writeFileSync(join(fixture, 'index.html'), originalHtml.replace('./styles/main.css', '/styles/main.css'));
    rewriteManifest(fixture);
    result = run(packageScript, ['--check', fixture]);
    assert.notEqual(result.status, 0);
  } finally {
    makeTreeWritable(fixture);
    rmSync(fixture, { recursive: true, force: true });
  }
});

test('interruption after old package rename restores it before cleaning temporary paths', () => {
  const oldMarker = '<!-- preserved old package -->\n';
  const canonicalManifest = readFileSync(join(artifact, 'SHA256SUMS'), 'utf8');
  writeFileSync(join(artifact, 'index.html'), `${oldMarker}${readFileSync(join(artifact, 'index.html'), 'utf8')}`);
  rewriteManifest(artifact);
  const oldCheck = run(packageScript, ['--check', artifact]);
  assert.equal(oldCheck.status, 0, oldCheck.stderr || oldCheck.stdout);

  const interrupted = run(packageScript, [], {
    env: {
      MK3K_PACKAGE_TEST_INTERRUPT_AFTER_BACKUP: '1',
      MK3K_PACKAGE_TEST_SECOND_TERM_DURING_RESTORE: '1',
    },
  });
  assert.notEqual(interrupted.status, 0);
  assert.match(readFileSync(join(artifact, 'index.html'), 'utf8'), /preserved old package/);
  const restoredCheck = run(packageScript, ['--check', artifact]);
  assert.equal(restoredCheck.status, 0, restoredCheck.stderr || restoredCheck.stdout);
  const leftovers = readdirSync(join(root, 'artifacts'))
    .filter((name) => name.startsWith('.mk3k-esp32-preview.'));
  assert.deepEqual(leftovers, []);

  const replaced = run(packageScript);
  assert.equal(replaced.status, 0, replaced.stderr || replaced.stdout);
  assert.doesNotMatch(readFileSync(join(artifact, 'index.html'), 'utf8'), /preserved old package/);
  assert.equal(readFileSync(join(artifact, 'SHA256SUMS'), 'utf8'), canonicalManifest);
  const replacementCheck = run(packageScript, ['--check', artifact]);
  assert.equal(replacementCheck.status, 0, replacementCheck.stderr || replacementCheck.stdout);
  const replacementLeftovers = readdirSync(join(root, 'artifacts'))
    .filter((name) => name.startsWith('.mk3k-esp32-preview.'));
  assert.deepEqual(replacementLeftovers, []);
});

test('signal during old backup cleanup is ignored after installing new package', () => {
  const oldMarker = '<!-- old package must not return -->\n';
  writeFileSync(join(artifact, 'index.html'), `${oldMarker}${readFileSync(join(artifact, 'index.html'), 'utf8')}`);
  rewriteManifest(artifact);

  const interrupted = run(packageScript, [], {
    env: { MK3K_PACKAGE_TEST_INTERRUPT_DURING_BACKUP_CLEANUP: '1' },
  });
  assert.equal(interrupted.status, 0, interrupted.stderr || interrupted.stdout);
  assert.doesNotMatch(readFileSync(join(artifact, 'index.html'), 'utf8'), /old package must not return/);
  const currentCheck = run(packageScript, ['--check', artifact]);
  assert.equal(currentCheck.status, 0, currentCheck.stderr || currentCheck.stdout);

  const backups = readdirSync(join(root, 'artifacts'))
    .filter((name) => name.startsWith('.mk3k-esp32-preview.previous.'));
  assert.deepEqual(backups, []);
});

test('deploy and rollback atomically swap validated releases and restore on nginx failure', () => {
  const fixture = mkdtempSync(join(tmpdir(), 'mk3k-deploy-'));
  const bin = join(fixture, 'bin');
  const remote = join(fixture, 'remote');
  const log = join(fixture, 'service.log');
  const chownLog = join(fixture, 'chown.log');
  mkdirSync(bin);
  mkdirSync(remote);
  writeFileSync(join(bin, 'ssh'), `#!/bin/sh\n[ "$1" = "-T" ] && shift\nshift\n"$@"\nstatus=$?\ncase " $* " in\n  *" tar -xf "*)\n    [ -z "${'$'}{FAKE_REMOTE_FIFO:-}" ] || mkfifo "${'$'}FAKE_REMOTE_FIFO"\n    [ -z "${'$'}{FAKE_REMOTE_EXTRA_FILE:-}" ] || printf 'unexpected\\n' > "${'$'}FAKE_REMOTE_EXTRA_FILE"\n    ;;\nesac\nexit "${'$'}status"\n`);
  writeFileSync(join(bin, 'sudo'), '#!/bin/sh\nexec "$@"\n');
  writeFileSync(join(bin, 'chown'), `#!/bin/sh\nprintf '%s\\n' "$*" >> "${chownLog}"\nexit 0\n`);
  writeFileSync(join(bin, 'nginx'), `#!/bin/sh\nif [ -n "${'$'}{FAKE_NGINX_FAIL_ONCE_FILE:-}" ] && [ -e "${'$'}FAKE_NGINX_FAIL_ONCE_FILE" ]; then rm -f "${'$'}FAKE_NGINX_FAIL_ONCE_FILE"; exit 1; fi\nexit 0\n`);
  writeFileSync(join(bin, 'systemctl'), `#!/bin/sh\nprintf '%s\\n' "$*" >> "${log}"\nif [ -n "${'$'}{FAKE_RELOAD_FAIL_ONCE_FILE:-}" ] && [ -e "${'$'}FAKE_RELOAD_FAIL_ONCE_FILE" ]; then rm -f "${'$'}FAKE_RELOAD_FAIL_ONCE_FILE"; exit 1; fi\n`);
  for (const name of ['ssh', 'sudo', 'chown', 'nginx', 'systemctl']) chmodSync(join(bin, name), 0o755);
  const env = {
    PATH: `${bin}:${process.env.PATH}`,
    MK3K_SSH_BIN: join(bin, 'ssh'),
    MK3K_REMOTE_BASE: remote,
  };
  try {
    let result = run(deployScript, ['--host', 'fake.test', '--release', 'r1', artifact], { env });
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.equal(readlinkSync(join(remote, 'current')), 'releases/r1');
    assert.equal(existsSync(join(remote, 'previous')), false);
    assert.equal(readFileSync(chownLog, 'utf8').trim(), `-R root:root ${remote}/.incoming-r1`);
    assert.equal(statSync(join(remote, 'releases/r1')).mode & 0o777, 0o555);
    assert.equal(statSync(join(remote, 'releases/r1/index.html')).mode & 0o777, 0o444);

    result = run(deployScript, ['--host', 'fake.test', '--release', 'bad-fifo', artifact], {
      env: { ...env, FAKE_REMOTE_FIFO: join(remote, '.incoming-bad-fifo/unexpected-pipe') },
    });
    assert.notEqual(result.status, 0);
    assert.equal(readlinkSync(join(remote, 'current')), 'releases/r1');
    assert.equal(existsSync(join(remote, 'releases/bad-fifo')), false);
    assert.equal(existsSync(join(remote, '.incoming-bad-fifo/unexpected-pipe')), true);

    result = run(deployScript, ['--host', 'fake.test', '--release', 'bad-extra', artifact], {
      env: { ...env, FAKE_REMOTE_EXTRA_FILE: join(remote, '.incoming-bad-extra/unexpected.txt') },
    });
    assert.notEqual(result.status, 0);
    assert.equal(readlinkSync(join(remote, 'current')), 'releases/r1');
    assert.equal(existsSync(join(remote, 'releases/bad-extra')), false);
    assert.equal(existsSync(join(remote, '.incoming-bad-extra/unexpected.txt')), true);

    result = run(deployScript, ['--host', 'fake.test', '--release', 'r2', artifact], { env });
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.equal(readlinkSync(join(remote, 'current')), 'releases/r2');
    assert.equal(readlinkSync(join(remote, 'previous')), 'releases/r1');

    result = run(deployScript, ['--host', 'fake.test', '--rollback'], { env });
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.equal(readlinkSync(join(remote, 'current')), 'releases/r1');
    assert.equal(readlinkSync(join(remote, 'previous')), 'releases/r2');

    result = run(deployScript, ['--host', 'fake.test', '--rollback'], { env });
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.equal(readlinkSync(join(remote, 'current')), 'releases/r2');
    assert.equal(readlinkSync(join(remote, 'previous')), 'releases/r1');

    const reloadsBeforeTransactionFailure = readFileSync(log, 'utf8');
    result = run(deployScript, ['--host', 'fake.test', '--release', 'tx-fail', artifact], {
      env: { ...env, MK3K_DEPLOY_TEST_FAIL_AFTER_PREVIOUS: '1' },
    });
    assert.notEqual(result.status, 0);
    assert.equal(readlinkSync(join(remote, 'current')), 'releases/r2');
    assert.equal(readlinkSync(join(remote, 'previous')), 'releases/r1');
    assert.equal(readFileSync(log, 'utf8'), reloadsBeforeTransactionFailure);
    assert.equal(existsSync(join(remote, 'releases/tx-fail')), true);

    result = run(deployScript, ['--host', 'fake.test', '--release', 'tx-term', artifact], {
      env: { ...env, MK3K_DEPLOY_TEST_TERM_AFTER_PREVIOUS: '1' },
    });
    assert.notEqual(result.status, 0);
    assert.equal(readlinkSync(join(remote, 'current')), 'releases/r2');
    assert.equal(readlinkSync(join(remote, 'previous')), 'releases/r1');
    assert.equal(readFileSync(log, 'utf8'), reloadsBeforeTransactionFailure);
    assert.equal(existsSync(join(remote, 'releases/tx-term')), true);

    const fail = join(fixture, 'fail-nginx-once');
    writeFileSync(fail, '1');
    result = run(deployScript, ['--host', 'fake.test', '--release', 'r3', artifact], {
      env: { ...env, FAKE_NGINX_FAIL_ONCE_FILE: fail },
    });
    assert.notEqual(result.status, 0);
    assert.equal(readlinkSync(join(remote, 'current')), 'releases/r2');
    assert.equal(readlinkSync(join(remote, 'previous')), 'releases/r1');
    assert.equal(existsSync(join(remote, 'releases/r1')), true);
    assert.equal(existsSync(join(remote, 'releases/r2')), true);
    assert.equal(existsSync(join(remote, 'releases/r3')), true);
    assert.equal(lstatSync(join(remote, 'current')).isSymbolicLink(), true);

    const reloadFail = join(fixture, 'fail-reload-once');
    writeFileSync(reloadFail, '1');
    result = run(deployScript, ['--host', 'fake.test', '--release', 'r4', artifact], {
      env: { ...env, FAKE_RELOAD_FAIL_ONCE_FILE: reloadFail },
    });
    assert.notEqual(result.status, 0);
    assert.equal(readlinkSync(join(remote, 'current')), 'releases/r2');
    assert.equal(readlinkSync(join(remote, 'previous')), 'releases/r1');
    assert.equal(existsSync(join(remote, 'releases/r4')), true);

    result = run(deployScript, ['--host', 'fake.test', artifact], { env });
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(readlinkSync(join(remote, 'current')), /^releases\/\d{8}T\d{6}Z-[0-9a-f]{12}$/);
    assert.equal(readlinkSync(join(remote, 'previous')), 'releases/r2');
    assert.equal(existsSync(join(remote, 'releases/r2')), true);
  } finally {
    makeTreeWritable(fixture);
    rmSync(fixture, { recursive: true, force: true });
  }
});

test('deploy rejects shell metacharacters in host and release identifiers', () => {
  assert.notEqual(run(deployScript, ['--host', 'good.test;id', artifact]).status, 0);
  assert.notEqual(run(deployScript, ['--host', 'good.test', '--release', 'bad;id', artifact]).status, 0);
});

test('nginx fragment is an isolated public no-cache prefix with WebAssembly MIME', () => {
  const fragment = readFileSync(nginxFragment, 'utf8');
  assert.match(fragment, /location = \/mk3k-esp32-preview/);
  assert.match(fragment, /location \^~ \/mk3k-esp32-preview\//);
  assert.match(fragment, /alias \/var\/www\/mk3k-esp32-preview\/current\//);
  assert.match(fragment, /application\/wasm wasm/);
  assert.match(fragment, /auth_basic off/);
  assert.match(fragment, /X-Robots-Tag "noindex, nofollow" always/);
  assert.match(fragment, /Cache-Control "no-cache" always/);
  assert.doesNotMatch(fragment, /proxy_pass|try_files/);
});
