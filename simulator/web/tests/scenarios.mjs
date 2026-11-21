const us = (value) => BigInt(value);

export const scenarios = [
  { name: 'empty-reset', actions: [{ type: 'reset', at: us(0) }] },
  { name: 'one-cyrillic-message', actions: [
    { type: 'reset', at: us(0) },
    { type: 'connect', at: us(10) },
    { type: 'notify', at: us(20), title: 'Алиса', subtitle: '', message: 'Привет' },
  ] },
  { name: 'six-messages-evict-oldest', actions: [
    { type: 'reset', at: us(0) },
    { type: 'connect', at: us(10) },
    ...['one', 'two', 'three', 'four', 'five', 'six'].map((message, index) => ({
      type: 'notify', at: us(20 + index * 10), title: String(index + 1), subtitle: '', message,
    })),
  ] },
  { name: 'screen-timeout-exact', actions: [
    { type: 'reset', at: us(0) },
    { type: 'connect', at: us(10) },
    { type: 'notify', at: us(20), title: 'Алиса', subtitle: '', message: 'Привет' },
    { type: 'tick', at: us(10_000_020) },
  ] },
  { name: 'a-older-b-newer', actions: [
    { type: 'reset', at: us(0) },
    { type: 'connect', at: us(10) },
    { type: 'notify', at: us(20), title: 'older', subtitle: '', message: 'A' },
    { type: 'notify', at: us(30), title: 'newer', subtitle: '', message: 'B' },
    { type: 'button-a', at: us(40) },
    { type: 'button-b-down', at: us(50) },
    { type: 'button-b-up', at: us(60) },
  ] },
  { name: 'hidden-previews', actions: [
    { type: 'reset', at: us(0) },
    { type: 'connect', at: us(10) },
    { type: 'previews-hidden', hidden: true },
    { type: 'notify', at: us(20), title: 'secret title', subtitle: 'secret subtitle', message: 'secret body' },
  ] },
  { name: 'disconnected-rejection', actions: [
    { type: 'reset', at: us(0) },
    { type: 'notify', at: us(10), title: 'rejected', subtitle: '', message: 'offline' },
  ] },
  { name: 'battery-and-unknown', actions: [
    { type: 'reset', at: us(0) },
    { type: 'battery', at: us(10), percent: 42 },
    { type: 'battery', at: us(20), percent: 255 },
  ] },
];

const actionKinds = {
  reset: 'ACTION_RESET', connect: 'ACTION_CONNECT', notify: 'ACTION_NOTIFY',
  tick: 'ACTION_TICK', 'button-a': 'ACTION_BUTTON_A',
  'button-b-down': 'ACTION_BUTTON_B_DOWN', 'button-b-up': 'ACTION_BUTTON_B_UP',
  battery: 'ACTION_BATTERY', 'previews-hidden': 'ACTION_PREVIEWS_HIDDEN',
};

export function cStringLiteral(value = '') {
  const bytes = new TextEncoder().encode(value);
  const escaped = Array.from(bytes, (byte) => `\\${byte.toString(8).padStart(3, '0')}`).join('');
  return `"${escaped}"`;
}

export function emitCHeader() {
  const lines = ['/* Generated from simulator/web/tests/scenarios.mjs. */'];
  scenarios.forEach((scenario, scenarioIndex) => {
    lines.push(`static const scenario_action_t scenario_${scenarioIndex}_actions[] = {`);
    scenario.actions.forEach((action) => {
      const at = action.at ?? 0n;
      lines.push(`    {${actionKinds[action.type]}, INT64_C(${at}), ${action.percent ?? 0}U, ${action.hidden ? 'true' : 'false'}, ${cStringLiteral(action.title)}, ${cStringLiteral(action.subtitle)}, ${cStringLiteral(action.message)}},`);
    });
    lines.push('};');
  });
  lines.push('static const scenario_t scenarios[] = {');
  scenarios.forEach((scenario, index) => {
    lines.push(`    {${cStringLiteral(scenario.name)}, scenario_${index}_actions, sizeof(scenario_${index}_actions) / sizeof(scenario_${index}_actions[0])},`);
  });
  lines.push('};', 'static const size_t scenario_count = sizeof(scenarios) / sizeof(scenarios[0]);');
  return `${lines.join('\n')}\n`;
}

if (process.argv[1] && import.meta.url === new URL(`file://${process.argv[1]}`).href) {
  if (process.argv[2] !== '--emit-c') throw new Error('usage: scenarios.mjs --emit-c');
  process.stdout.write(emitCHeader());
}
