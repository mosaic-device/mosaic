import { test } from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import { execFileSync } from 'node:child_process'
import {
  resolveTokens,
  deepMerge,
  compareThemePaths,
  parseColor,
  themeCss,
  firmwareColorDefines,
  firmwareGlobalDefines,
  firmwareFontApi,
  shadcnCss,
  tailwindThemeCss,
} from './build.mjs'

const read = (p) => JSON.parse(fs.readFileSync(new URL(p, import.meta.url)))
const global = read('tokens/global.json')
const primitive = read('tokens/primitive.json')
const theme = (name) =>
  resolveTokens(deepMerge(primitive, global, read(`tokens/color/${name}.json`)))
const dark = theme('dark')
const light = theme('light')
const value = (name) => dark.find((t) => t.name === name).value
const t = (value) => ({ $value: value })
const luminance = (color) => {
  const n = Number(parseColor(color).hex)
  return [16, 8, 0]
    .map((s) => ((n >> s) & 255) / 255)
    .map((c) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4))
    .reduce((sum, c, i) => sum + c * [0.2126, 0.7152, 0.0722][i], 0)
}
const contrast = (a, b) =>
  (Math.max(luminance(a), luminance(b)) + 0.05) /
  (Math.min(luminance(a), luminance(b)) + 0.05)

test('reference resolution rejects missing, embedded and circular references', () => {
  assert.throws(() => resolveTokens({ a: t('{missing}') }), /missing token/)
  assert.throws(() => resolveTokens({ a: t('bad {missing}') }), /Unresolved/)
  assert.throws(() => resolveTokens({ a: t('{b}'), b: t('{a}') }), /Circular/)
  assert.throws(() => resolveTokens({ a: t('{a}') }), /Circular/)
  assert.equal(
    resolveTokens({ a: t('12px'), b: t('{a.$value}') })[1].value,
    '12px',
  )
})

test('theme paths have parity and mismatch fails', () => {
  compareThemePaths(light, dark)
  assert.throws(
    () =>
      compareThemePaths(
        light,
        dark.filter((t) => t.name !== 'color.selected'),
      ),
    /Missing in dark/,
  )
})

test('approved palette and exact rounded reference mixes', () => {
  for (const [role, expected] of Object.entries({
    background: '#090A0A',
    surface: '#141516',
    surfaceRaised: '#222426',
    border: '#303335',
    foreground: '#F2F2EF',
    mutedForeground: '#92979B',
    lowEmphasis: '#666B70',
    primary: '#7540B5',
    primaryForeground: '#FFFFFF',
    focus: '#B0A3CF',
    primaryHover: '#7D4BB9',
    primaryPressed: '#67389F',
  })) {
    assert.equal(value(`color.${role}`), expected)
  }
  assert.equal(value('color.secondary'), value('color.surface'))
  assert.notEqual(value('color.info'), value('color.primary'))
})

test('CSS alpha and firmware RGB plus opacity survive generation', () => {
  const css = themeCss(':root', dark)
  const header = firmwareColorDefines(dark, 'DARK')
  for (const [role, alpha, opacity] of [
    ['selected', '0.08', 20],
    ['selectedBorder', '0.33', 84],
  ]) {
    const macro = role === 'selected' ? 'SELECTED' : 'SELECTED_BORDER'
    assert.equal(value(`color.${role}`), `rgba(176, 163, 207, ${alpha})`)
    assert.ok(css.includes(`rgba(176, 163, 207, ${alpha})`))
    assert.ok(header.includes(`#define TOKEN_COLOR_${macro}_DARK 0xB0A3CF`))
    assert.ok(
      header.includes(`#define TOKEN_COLOR_${macro}_OPA_DARK ${opacity}`),
    )
  }
  assert.throws(() => parseColor('rgba(256, 0, 0, 1)'), /outside/)
  assert.throws(() => parseColor('rgba(0, 0, 0, 1.1)'), /outside/)
})

test('web roles and explicit Manrope firmware mapping preserve existing API', () => {
  const header = firmwareGlobalDefines(dark)
  const font = firmwareFontApi(dark)
  for (const [role, web, device] of [
    ['xs', 12, 28],
    ['sm', 14, 32],
    ['md', 16, 36],
    ['lg', 20, 48],
    ['xl', 24, 60],
  ]) {
    assert.equal(value(`text.${role}`), `${web}px`)
    assert.ok(
      header.includes(
        `#define TOKEN_FIRMWARE_TEXT_${role.toUpperCase()} ${device}`,
      ),
    )
    assert.ok(
      font.includes(
        `case TOKEN_TEXT_${role.toUpperCase()}:\n        return ui_tokens_font_weight(${device}, weight);`,
      ),
    )
  }
  for (const size of [28, 32, 36, 48, 60]) {
    for (const weight of [400, 500, 600])
      assert.ok(font.includes(`&mosaic_manrope_${size}_${weight}`))
  }
  assert.ok(font.includes('return ui_tokens_get_font_weight(text_token, 400)'))
  assert.ok(font.includes('#define GET_FONT_WEIGHT'))
  assert.ok(!font.includes('montserrat'))
  assert.ok(font.includes('default: return &mosaic_manrope_36_400;'))
  assert.ok(
    font.includes(
      'default:\n        return ui_tokens_font_weight(36, weight);',
    ),
  )
  for (const [role, px] of Object.entries({
    'space.card': 24,
    'space.control': 20,
    'space.gap': 24,
    'space.screen': 24,
    'icon.md': 40,
    'wordmark.md': 40,
    'wordmark.lg': 60,
  }))
    assert.equal(value('firmware.' + role), px + 'px')
  assert.equal(value('control.md'), '40px')
  assert.equal(value('firmware.control.md'), '80px')
  assert.equal(value('typography.body'), value('text.md'))
  assert.match(value('font.family.display'), /^Manrope/)
  assert.match(value('font.family.brand'), /^Outfit/)
  assert.ok(tailwindThemeCss(dark).includes('--font-brand: Outfit'))
  assert.ok(themeCss(':root', dark).includes('--focus-width: 2px'))
  for (const tokens of [dark, light]) {
    const css = themeCss(':root', tokens)
    const defines = firmwareGlobalDefines(tokens)
    assert.equal(tokens.find((t) => t.name === 'focus.offset').value, '4px')
    assert.ok(css.includes('--focus-offset: 4px'))
    assert.ok(defines.includes('#define TOKEN_FOCUS_OFFSET 4'))
    for (const role of ['heading', 'title']) {
      assert.equal(
        tokens.find((t) => t.name === `typography.weight.${role}`).value,
        '500',
      )
      assert.ok(css.includes(`--typography-weight-${role}: 500`))
      assert.ok(
        defines.includes(
          `#define TOKEN_TYPOGRAPHY_WEIGHT_${role.toUpperCase()} 500`,
        ),
      )
    }
    assert.equal(
      tokens.find((t) => t.name === 'font.weight.semibold').value,
      '600',
    )
    assert.ok(defines.includes('#define TOKEN_FONT_WEIGHT_SEMIBOLD 600'))
  }
})

test('shadcn adapter uses semantic full colors, neutral accent and independent danger', () => {
  const css = shadcnCss()
  for (const [role, semantic] of [
    ['card', 'surface'],
    ['popover', 'surface-raised'],
    ['secondary', 'secondary'],
    ['accent', 'accent'],
    ['destructive', 'danger'],
    ['ring', 'focus'],
    ['input', 'field'],
  ]) {
    assert.ok(css.includes(`--${role}: var(--color-${semantic});`))
  }
})

test('contrast audit: action, status, essential text and focus/boundaries', () => {
  for (const role of ['primary', 'primaryHover', 'primaryPressed'])
    assert.ok(
      contrast(value('color.primaryForeground'), value(`color.${role}`)) >= 4.5,
      role,
    )
  for (const role of ['info', 'success', 'warning', 'danger'])
    assert.ok(
      contrast(value(`color.${role}Foreground`), value(`color.${role}`)) >= 4.5,
      role,
    )
  for (const bg of ['background', 'surface', 'surfaceRaised']) {
    for (const fg of ['foreground', 'mutedForeground'])
      assert.ok(
        contrast(value(`color.${fg}`), value(`color.${bg}`)) >= 4.5,
        `${fg}/${bg}`,
      )
    for (const fg of ['focus', 'interactiveBorder'])
      assert.ok(contrast(value(`color.${fg}`), value(`color.${bg}`)) >= 3)
  }
  assert.ok(contrast(value('color.lowEmphasis'), value('color.surface')) < 4.5)
  assert.ok(contrast(value('color.border'), value('color.surface')) < 3)
})

test('generation is deterministic and artifacts match renderers', () => {
  const files = [
    'dist/css/dark.css',
    'dist/css/light.css',
    'dist/css/tokens.css',
    'dist/css/tailwind-theme.css',
    'dist/css/shadcn.css',
    'dist/js/eslint-restricted.js',
    'dist/js/eslint-restricted.d.ts',
    '../../../firmware/main/ui/generated/design_tokens.h',
  ]
  const snapshot = () =>
    files.map((p) => fs.readFileSync(new URL(p, import.meta.url), 'utf8'))
  execFileSync(process.execPath, ['build.mjs'], {
    cwd: new URL('.', import.meta.url),
  })
  const before = snapshot()
  execFileSync(process.execPath, ['build.mjs'], {
    cwd: new URL('.', import.meta.url),
  })
  assert.deepEqual(snapshot(), before)
  assert.equal(before[0], themeCss(':root, [data-theme="dark"]', dark))
  assert.equal(before[4], shadcnCss())
})
