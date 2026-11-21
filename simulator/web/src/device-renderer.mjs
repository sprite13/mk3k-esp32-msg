export const COLORS = Object.freeze({
  off: '#000000',
  background: '#070a0e',
  text: '#eef2f7',
  title: '#ffc44a',
  subtitle: '#4fd1c5',
  connected: '#66d68b',
  warning: '#ffb74d',
  error: '#ff5c5c',
});

const FONT_WIDTH = 12;
const FONT_HEIGHT = 20;
const TOP_MARGIN = 7;
const LANDSCAPE_WIDTH = 240;
const LANDSCAPE_HEIGHT = 135;

function lineColor(style, connection) {
  switch (style) {
    case 1: return COLORS.warning;
    case 2: return COLORS.title;
    case 3: return COLORS.subtitle;
    case 4: return COLORS.text;
    case 0:
    default:
      if (connection === 1) return COLORS.connected;
      if (connection === 3) return COLORS.error;
      return COLORS.warning;
  }
}

export function paintGlyph(context, x, y, rows, color) {
  context.fillStyle = color;
  for (let row = 0; row < FONT_HEIGHT; row += 1) {
    for (let pixel = 0; pixel < FONT_WIDTH; pixel += 1) {
      if ((rows[row] & (1 << (FONT_WIDTH - 1 - pixel))) !== 0) {
        context.fillRect(x + pixel, y + row, 1, 1);
      }
    }
  }
}

export function drawFirmwareFrame(context, snapshot, plan, glyphFor) {
  context.fillStyle = snapshot.displayAwake ? COLORS.background : COLORS.off;
  context.fillRect(0, 0, LANDSCAPE_WIDTH, LANDSCAPE_HEIGHT);
  if (!snapshot.displayAwake) return;
  plan.forEach((line, row) => {
    const color = lineColor(line.style, snapshot.connection);
    line.glyphs.forEach((codepoint, column) => {
      const glyph = glyphFor(codepoint);
      if (glyph) paintGlyph(context, column * FONT_WIDTH,
        TOP_MARGIN + row * FONT_HEIGHT, glyph, color);
    });
  });
}

export function renderDeviceScreen(canvas, snapshot, plan, glyphFor) {
  const context = canvas.getContext('2d', { alpha: false });
  context.imageSmoothingEnabled = false;
  context.save();
  context.translate(canvas.width, 0);
  context.rotate(Math.PI / 2);
  drawFirmwareFrame(context, snapshot, plan, glyphFor);
  context.restore();
}
