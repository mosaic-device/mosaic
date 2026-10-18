import fs from 'node:fs'
import path from 'node:path'
import { createRequire } from 'node:module'
import { fileURLToPath } from 'node:url'
import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
const root = fileURLToPath(new URL('../', import.meta.url))
process.chdir(root)
const require = createRequire(import.meta.url)
const fontkit = require('fontkit')
const sharp = require('sharp')
const out = path.resolve(root, '../../../firmware/main/ui/generated/assets')
fs.mkdirSync(out, { recursive: true })
const manifest = JSON.parse(fs.readFileSync('sources/manifest.json'))
for (const s of manifest.sources) {
  if (
    createHash('sha256').update(fs.readFileSync(s.file)).digest('hex') !==
    s.sha256
  )
    throw Error(`Source checksum: ${s.file}`)
}
const firmware = JSON.parse(
  fs.readFileSync('../tokens/tokens/global.json'),
).firmware
const pixels = (token) => {
  if (!/^\d+px$/.test(token.$value))
    throw Error('Expected positive pixel dimension')
  const value = Number.parseInt(token.$value, 10)
  if (value <= 0) throw Error('Expected positive pixel dimension')
  return value
}
const sizes = [...new Set(Object.values(firmware.text).map(pixels))]
fs.mkdirSync('metadata', { recursive: true })
const previousOutputs = fs.existsSync('metadata/outputs.json')
  ? JSON.parse(fs.readFileSync('metadata/outputs.json'))
  : []
const adminLicenses = path.resolve(root, '../../apps/admin/public/licenses')
fs.mkdirSync(adminLicenses, { recursive: true })
const licenseFiles = ['Manrope-OFL.txt', 'Outfit-OFL.txt', 'Lucide-LICENSE']
fs.mkdirSync(path.join(out, 'licenses'), { recursive: true })
fs.mkdirSync('licenses', { recursive: true })
for (const file of licenseFiles) {
  fs.copyFileSync(`sources/${file}`, path.join(out, 'licenses', file))
  fs.copyFileSync(`sources/${file}`, `licenses/${file}`)
  if (file !== 'Lucide-LICENSE')
    fs.copyFileSync(`sources/${file}`, path.join(adminLicenses, file))
}
fs.writeFileSync(
  path.join(out, 'README.md'),
  `# Generated Mosaic firmware assets\n\nGenerated offline with @mosaic/assets. Do not edit generated files.\nCommitted outputs are covered by platform/packages/assets/metadata/outputs.json; canonical\nlicenses remain in platform/packages/assets/licenses/.\n\nSources, pinned upstream URLs/revisions/checksums and generation tools are in\nplatform/packages/assets/. See that package's README.md for the explicit\npnpm generate-assets maintenance command and pinned Python environment setup.\nNormal builds consume committed assets without running converters.\n\n## Distribution notices\n\nKeep this README and all three files in licenses/ with redistributed generated\nasset source packages. For firmware binary/device releases, include these full\nnotices in the accompanying release documentation or third-party notices.\nThe build does not embed these text sidecars in the firmware image.\n\n- mosaic_manrope_*.c: derived from Manrope, SIL Open Font License 1.1;\n  copyright and full terms in licenses/Manrope-OFL.txt. Conversion changes\n  weight/size, normalizes digit advances and rasterizes glyphs.\n- mosaic_wordmark_*.c: outlined/rasterized Outfit 500 MOSAIC wordmark;\n  source font copyright and SIL OFL 1.1 in licenses/Outfit-OFL.txt.\n- mosaic_icon_settings_${pixels(firmware.icon.md)}.c: Lucide settings, release 0.468.0;\n  copyright and full terms in licenses/Lucide-LICENSE.\n\nThese notices accompany the upstream-derived assets; they do not relicense\nunrelated Mosaic code. Preserve the full notices when copying assets elsewhere.\n`,
)
function fontNotice(family) {
  const copyright = fontkit.openSync(`sources/${family}.ttf`).copyright
  const license = `${family}-OFL.txt`
  const sourceCopyright = fs
    .readFileSync(`sources/${license}`, 'utf8')
    .split('\n')[0]
  return `/*\n * Derived from ${family}. ${copyright}\n * ${sourceCopyright}\n * Licensed under the SIL Open Font License, Version 1.1.\n * Full copyright notice and license: licenses/${license}.\n * Distribute that sidecar with this asset; see README.md.\n */\n`
}
const manropeNotice = fontNotice('Manrope')
const outfitNotice = fontNotice('Outfit')
const lucideNotice =
  '/* Derived from Lucide settings 0.468.0.\n * ISC license and copyright notice: licenses/Lucide-LICENSE.\n * Distribute that sidecar with this asset; see README.md.\n */\n'
const weights = [400, 500, 600]
const names = []
const coverage = []
for (const weight of weights) {
  const font = fontkit.openSync(`sources/Manrope-${weight}.ttf`)
  const cps = [
    ...Array.from({ length: 0x180 - 0x20 }, (_, i) => i + 0x20),
    ...Array.from({ length: 0x70 }, (_, i) => i + 0x2000),
    0x20ac,
    0x2212,
    0xfffd,
  ]
  const supported = cps.filter((cp) => font.hasGlyphForCodePoint(cp))
  const missing = cps.filter((cp) => !font.hasGlyphForCodePoint(cp))
  coverage.push({
    weight,
    supported,
    missing,
    digitAdvances: [...'0123456789'].map(
      (c) => font.glyphForCodePoint(c.codePointAt(0)).advanceWidth,
    ),
  })
  for (const size of sizes) {
    const name = `mosaic_manrope_${size}_${weight}`
    names.push(name)
    execFileSync(process.execPath, [
      require.resolve('lv_font_conv/lv_font_conv.js'),
      '--font',
      `sources/Manrope-${weight}.ttf`,
      '--size',
      String(size),
      '--bpp',
      '4',
      '--format',
      'lvgl',
      '--lv-include',
      'lvgl.h',
      '--range',
      supported.join(','),
      '--no-compress',
      '--lv-font-name',
      name,
      '-o',
      path.join(out, `${name}.c`),
    ])
    // lv_font_conv embeds its absolute output path in the banner; normalize it.
    const file = path.join(out, `${name}.c`)
    fs.writeFileSync(
      file,
      manropeNotice + fs.readFileSync(file, 'utf8').replaceAll(out + '/', ''),
    )
  }
}
fs.writeFileSync(
  'metadata/coverage.json',
  JSON.stringify(coverage, null, 2) + '\n',
)
// Outline using font glyphs, kerning and advances; tracking is between letters only.
const font = fontkit.openSync('sources/Outfit-500.ttf')
const run = font.layout('MOSAIC')
let x = 0
const paths = run.glyphs.map((glyph, i) => {
  const position = run.positions[i]
  const svg = `<path transform="translate(${x + position.xOffset},${position.yOffset})" d="${glyph.path.toSVG()}"/>`
  x +=
    position.xAdvance + (i < run.glyphs.length - 1 ? font.unitsPerEm * 0.23 : 0)
  return svg
})
const top = Math.max(...run.glyphs.map((g) => g.bbox.maxY))
const bottom = Math.min(...run.glyphs.map((g) => g.bbox.minY))
const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${x} ${top - bottom}" fill="white"><g transform="translate(0,${top}) scale(1,-1)">${paths.join('')}</g></svg>\n`
fs.writeFileSync('web/mosaic-wordmark.svg', svg)
const images = []
async function alpha(name, input, height, notice) {
  const { data, info } = await sharp(input)
    .resize({ height })
    .ensureAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true })
  const bytes = Array.from(
    { length: info.width * info.height },
    (_, i) => data[i * info.channels + info.channels - 1],
  )
  fs.writeFileSync(
    path.join(out, `${name}.c`),
    `${notice}/* Generated by @mosaic/assets; LVGL 9 A8, tint with image_recolor + OPA_COVER. */\n#include "lvgl.h"\nstatic const uint8_t pixels[] = {\n${bytes.map((b, i) => (i % 24 === 0 ? '\n' : '') + b).join(',')}\n};\nconst lv_image_dsc_t ${name} = {\n .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_A8, .flags = 0, .w = ${info.width}, .h = ${info.height}, .stride = ${info.width} },\n .data_size = sizeof(pixels), .data = pixels,\n};\n`,
  )
  images.push({
    name,
    width: info.width,
    height: info.height,
    bytes: bytes.length,
  })
}
await alpha(
  `mosaic_icon_settings_${pixels(firmware.icon.md)}`,
  Buffer.from(
    fs
      .readFileSync('sources/settings.svg', 'utf8')
      .replaceAll('currentColor', 'white'),
  ),
  pixels(firmware.icon.md),
  lucideNotice,
)
for (const size of [...new Set(Object.values(firmware.wordmark).map(pixels))])
  await alpha(`mosaic_wordmark_${size}`, Buffer.from(svg), size, outfitNotice)
fs.writeFileSync(
  path.join(out, 'assets.h'),
  `/* Generated by @mosaic/assets. */\n#pragma once\n#include "lvgl.h"\n#ifdef __cplusplus\nextern "C" {\n#endif\n${names.map((n) => `LV_FONT_DECLARE(${n});`).join('\n')}\n${images.map((i) => `LV_IMAGE_DECLARE(${i.name});`).join('\n')}\n#ifdef __cplusplus\n}\n#endif\n`,
)
const sources = [...names, ...images.map((i) => i.name)].map((n) => `${n}.c`)
fs.writeFileSync(
  path.join(out, 'sources.cmake'),
  `# Generated; explicit sources, no build-time generator.\nset(MOSAIC_ASSET_SOURCES\n${sources.map((n) => `    "ui/generated/assets/${n}"`).join('\n')}\n)\n`,
)
fs.writeFileSync(
  'web/fonts.css',
  [
    ...weights.map(
      (w) =>
        `@font-face { font-family: Manrope; font-style: normal; font-weight: ${w}; font-display: swap; src: url('./manrope-${w}.woff2') format('woff2'); }`,
    ),
    "@font-face { font-family: Outfit; font-style: normal; font-weight: 500; font-display: swap; src: url('./outfit-500.woff2') format('woff2'); }",
    '',
  ].join('\n'),
)
const outputs = [
  ...sources.map((n) => path.join(out, n)),
  path.join(out, 'assets.h'),
  path.join(out, 'sources.cmake'),
  path.join(out, 'README.md'),
  ...licenseFiles.map((file) => path.join(out, 'licenses', file)),
  ...licenseFiles.map((file) => path.resolve('licenses', file)),
  ...licenseFiles
    .filter((file) => file !== 'Lucide-LICENSE')
    .map((file) => path.join(adminLicenses, file)),
  ...fs.readdirSync('web').map((n) => path.resolve('web', n)),
  path.resolve('metadata/coverage.json'),
]
// Remove only recognized obsolete generated C files from the previous manifest.
for (const entry of previousOutputs) {
  const file = path.resolve(root, entry.file)
  if (
    path.dirname(file) === out &&
    /^mosaic_(?:manrope_\d+_(?:400|500|600)|wordmark_\d+|icon_settings_\d+)\.c$/.test(
      path.basename(file),
    ) &&
    !outputs.includes(file)
  )
    fs.rmSync(file, { force: true })
}
fs.writeFileSync(
  'metadata/outputs.json',
  JSON.stringify(
    outputs.map((file) => ({
      file: path.relative(root, file),
      bytes: fs.statSync(file).size,
      sha256: createHash('sha256').update(fs.readFileSync(file)).digest('hex'),
    })),
    null,
    2,
  ) + '\n',
)
console.log(
  `Generated ${names.length} fonts and ${images.length} alpha assets.`,
)
