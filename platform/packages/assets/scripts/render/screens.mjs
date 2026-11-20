// Draws the device's notification screens as PNGs for the renders: the same
// layout as the site's DeviceScreen, 640 × 360 drawn at 2×, with text outlined
// from the committed Manrope and Outfit fonts so no system fonts are involved.
import fs from 'node:fs'
import { createRequire } from 'node:module'
import { fileURLToPath } from 'node:url'
import sharp from 'sharp'

const { openSync } = createRequire(import.meta.url)('fontkit')
const source = (name) =>
  fileURLToPath(new URL(`../../sources/${name}`, import.meta.url))
const fonts = {
  400: openSync(source('Manrope-400.ttf')),
  500: openSync(source('Manrope-500.ttf')),
  brand: openSync(source('Outfit-500.ttf')),
}

const C = {
  bg: '#090A0A',
  fg: '#F2F2EF',
  muted: '#92979B',
  low: '#666B70',
  raised: '#222426',
  surface: '#141516',
  border: '#303335',
  primary: '#7540B5',
  add: '#4ADE80',
  del: '#F87171',
}

/** Text as outlined paths; returns the SVG and the advance width. */
function text(
  str,
  x,
  baseline,
  size,
  { weight = 400, fill = C.fg, spacing = 0, anchor = 'start' } = {},
) {
  const font = fonts[weight]
  const run = font.layout(str)
  const scale = size / font.unitsPerEm
  let width = 0
  for (const p of run.positions) width += p.xAdvance * scale + spacing
  width -= spacing
  let pen = anchor === 'end' ? x - width : x
  const paths = run.glyphs.map((glyph, i) => {
    const p = run.positions[i]
    const d = glyph.path.toSVG()
    const gx = pen + p.xOffset * scale
    pen += p.xAdvance * scale + spacing
    return d
      ? `<path transform="translate(${gx.toFixed(2)} ${baseline}) scale(${scale} ${-scale})" d="${d}"/>`
      : ''
  })
  return { svg: `<g fill="${fill}">${paths.join('')}</g>`, width }
}

const rect = (x, y, w, h, r, fill, stroke) =>
  `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="${r}" fill="${fill}"${stroke ? ` stroke="${stroke}"` : ''}/>`

function buttons(labels) {
  let x = 26
  const y = 360 - 22 - 44
  return labels
    .map((label, i) => {
      const pad = i === 0 ? 22 : i === 1 ? 20 : 14
      const t = text(label, 0, 0, 15, { weight: 500 })
      const w = t.width + pad * 2
      const fill = i === 0 ? '#FFFFFF' : i === 1 ? C.fg : C.muted
      const back =
        i === 0
          ? rect(x, y, w, 44, 8, C.primary)
          : i === 1
            ? rect(x + 0.5, y + 0.5, w - 1, 43, 8, C.surface, C.border)
            : ''
      const svg =
        back + text(label, x + pad, y + 27, 15, { weight: 500, fill }).svg
      x += w + 10
      return svg
    })
    .join('')
}

function sourceRow(name, detail) {
  const name_ = text(name, 64, 89, 13, { weight: 500 })
  return (
    rect(26, 70, 28, 28, 8, C.raised) +
    `<rect x="34" y="78" width="12" height="12" fill="none" stroke="${C.fg}" stroke-width="1.5"/>` +
    name_.svg +
    text(detail, 64 + name_.width + 10, 89, 13, { fill: C.muted }).svg
  )
}

const variants = {
  review: () => {
    let x = 26
    const meta = [
      ['+412', C.add],
      ['−88', C.del],
      ['12 files', C.muted],
      ['2 min ago', C.muted],
    ]
      .map(([t, fill]) => {
        const r = text(t, x, 207, 13, { fill })
        x += r.width + 12
        return r.svg
      })
      .join('')
    return (
      sourceRow('GitHub', 'mosaic-device/mosaic') +
      text('Avery requested your review', 26, 142, 30, { weight: 500 }).svg +
      text(
        'Show and run notification actions on the device · #194',
        26,
        178,
        16,
        { fill: C.muted },
      ).svg +
      meta +
      buttons(['Approve', 'Open on laptop', 'Dismiss'])
    )
  },
  calendar: () =>
    sourceRow('Google Calendar', '10:00 – 10:15') +
    text('Firmware standup', 26, 142, 30, { weight: 500 }).svg +
    text('in 4 min', 24, 214, 64, { weight: 500 }).svg +
    buttons(['Join on laptop', 'Snooze 5 min', 'Dismiss']),
}

function screen(variant) {
  const bar =
    text('MOSAIC', 26, 35, 13, { weight: 'brand', spacing: 4.4 }).svg +
    text('9:50', 614, 35, 13, { fill: C.muted, anchor: 'end' }).svg +
    rect(540, 28, 14, 4, 2, C.fg) +
    rect(558, 28, 4, 4, 2, C.low) +
    rect(566, 28, 4, 4, 2, C.low)
  return `<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="720" viewBox="0 0 640 360">
<rect width="640" height="360" fill="${C.bg}"/>${bar}${variants[variant]()}</svg>`
}

/** Writes review.png and calendar.png into `dir`; returns their paths. */
export async function drawScreens(dir) {
  fs.mkdirSync(dir, { recursive: true })
  const out = {}
  for (const variant of Object.keys(variants)) {
    out[variant] = `${dir}/${variant}.png`
    await sharp(Buffer.from(screen(variant)))
      .png()
      .toFile(out[variant])
  }
  return out
}
