import { test } from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import { createHash } from 'node:crypto'
import { createRequire } from 'node:module'
const { openSync } = createRequire(import.meta.url)('fontkit')
const root = new URL('../', import.meta.url)
const read = (file) => fs.readFileSync(new URL(file, root))
const json = (file) => JSON.parse(read(file))
const hash = (file) => createHash('sha256').update(read(file)).digest('hex')
test('committed outputs match checksums and sizes', () => {
  const outputs = json('metadata/outputs.json')
  assert.ok(outputs.length > 0)
  assert.ok(outputs.some((o) => o.file === 'metadata/coverage.json'))
  assert.ok(!outputs.some((o) => o.file === 'metadata/outputs.json'))
  assert.equal(new Set(outputs.map((o) => o.file)).size, outputs.length)
  for (const o of outputs) {
    assert.equal(hash(o.file), o.sha256, o.file)
    assert.equal(read(o.file).length, o.bytes, o.file)
  }
  for (const name of fs.readdirSync(new URL('web/', root)))
    assert.ok(
      outputs.some((o) => o.file === `web/${name}`),
      name,
    )
  for (const name of fs.readdirSync(new URL('licenses/', root)))
    assert.ok(
      outputs.some((o) => o.file === `licenses/${name}`),
      name,
    )
})
test('firmware distribution bundles original license sidecars with checksums and per-asset references', () => {
  const prefix = '../../../firmware/main/ui/generated/assets/'
  const outputs = json('metadata/outputs.json')
  for (const file of ['Manrope-OFL.txt', 'Outfit-OFL.txt', 'Lucide-LICENSE']) {
    const source = `licenses/${file}`
    const generated = `${prefix}licenses/${file}`
    assert.deepEqual(read(generated), read(source))

    assert.equal(
      outputs.find((o) => o.file === generated)?.sha256,
      hash(source),
    )
  }
  const readme = `${prefix}README.md`
  assert.ok(outputs.some((o) => o.file === readme))

  for (const size of Object.values(
    json('../tokens/tokens/global.json').firmware.text,
  ).map((t) => Number.parseInt(t.$value, 10))) {
    for (const weight of [400, 500, 600]) {
      const c = read(`${prefix}mosaic_manrope_${size}_${weight}.c`).toString()
      assert.match(c, /Copyright 2019 The Manrope Project Authors/)
      assert.match(c, /SIL Open Font License, Version 1\.1/)
      assert.ok(c.includes('licenses/Manrope-OFL.txt'))
    }
  }
  const firmware = json('../tokens/tokens/global.json').firmware
  for (const size of new Set(
    Object.values(firmware.wordmark).map((t) => Number.parseInt(t.$value, 10)),
  ))
    assert.ok(
      read(`${prefix}mosaic_wordmark_${size}.c`)
        .toString()
        .includes('licenses/Outfit-OFL.txt'),
    )
  assert.ok(
    read(
      `${prefix}mosaic_icon_settings_${Number.parseInt(firmware.icon.md.$value, 10)}.c`,
    )
      .toString()
      .includes('licenses/Lucide-LICENSE'),
  )
})
test('recorded font coverage includes required characters and tabular digit advances', () => {
  assert.ok(json('metadata/coverage.json').length > 0)
  for (const c of json('metadata/coverage.json')) {
    for (const ch of 'Mosaic Café ĀāŁłŽž 0123456789 ° “”—–… € −')
      assert.ok(c.supported.includes(ch.codePointAt(0)), ch)
    assert.equal(new Set(c.digitAdvances).size, 1)
    assert.ok(!c.supported.includes(0x4e00))
  }
})
test('web fonts are WOFF2 with local CSS references and an outlined wordmark', () => {
  for (const file of [
    'manrope-400',
    'manrope-500',
    'manrope-600',
    'outfit-500',
  ])
    assert.equal(read(`web/${file}.woff2`).subarray(0, 4).toString(), 'wOF2')
  assert.ok(
    !/(?:url\(\s*['"]?|@import\s*['"])(?:https?:)?\/\//.test(
      read('web/fonts.css').toString(),
    ),
    'Font CSS must not fetch external fonts or styles',
  )
  const svg = read('web/mosaic-wordmark.svg').toString()
  assert.ok(svg.includes('<path'))
  assert.ok(!svg.includes('<text'))
})

test('generated declarations, source list and manifest match firmware token sizes exactly', () => {
  const firmware = json('../tokens/tokens/global.json').firmware
  const px = (t) => Number.parseInt(t.$value, 10)
  const prefix = '../../../firmware/main/ui/generated/assets/'
  const generatedFiles = fs.readdirSync(new URL(prefix, root))
  const expected = [
    ...[...new Set(Object.values(firmware.text).map(px))].flatMap((size) =>
      [400, 500, 600].map((weight) => `mosaic_manrope_${size}_${weight}.c`),
    ),
    ...generatedFiles.filter((name) => /^mosaic_icon_.*\.c$/.test(name)),
    ...[...new Set(Object.values(firmware.wordmark).map(px))].map(
      (size) => `mosaic_wordmark_${size}.c`,
    ),
  ].sort()
  const cmake = read(prefix + 'sources.cmake').toString()
  assert.deepEqual(
    [...cmake.matchAll(/ui\/generated\/assets\/([^"\n]+\.c)/g)]
      .map((m) => m[1])
      .sort(),
    expected,
  )
  assert.deepEqual(
    json('metadata/outputs.json')
      .filter((o) => o.file.startsWith(prefix) && o.file.endsWith('.c'))
      .map((o) => o.file.slice(prefix.length))
      .sort(),
    expected,
  )
  assert.deepEqual(
    generatedFiles.filter((f) => /^mosaic_.*\.c$/.test(f)).sort(),
    expected,
  )
  const header = read(prefix + 'assets.h').toString()
  for (const name of expected)
    assert.ok(header.includes(`(${name.slice(0, -2)});`), name)
  const icon = read(
    prefix + `mosaic_icon_settings_${px(firmware.icon.md)}.c`,
  ).toString()
  assert.ok(icon.includes(`.h = ${px(firmware.icon.md)}`))
  for (const token of Object.values(firmware.wordmark))
    assert.ok(
      read(prefix + `mosaic_wordmark_${px(token)}.c`)
        .toString()
        .includes(`.h = ${px(token)}`),
    )
})

test('pinned upstream sources match checksums and canonical notices', () => {
  for (const source of json('sources/manifest.json').sources) {
    assert.equal(hash(source.file), source.sha256, source.file)
    assert.match(
      source.url,
      /^https:\/\/raw\.githubusercontent\.com\/(google\/fonts|lucide-icons\/lucide)\//,
    )
    if (/OFL\.txt$|Lucide-LICENSE$/.test(source.file))
      assert.deepEqual(
        read(source.file),
        read(source.file.replace('sources/', 'licenses/')),
      )
  }
})

test('WOFF2 retains upstream copyright, attribution and license metadata with full OFL text', () => {
  for (const [family, weights] of [
    ['Manrope', [400, 500, 600]],
    ['Outfit', [500]],
  ]) {
    const source = openSync(new URL(`sources/${family}.ttf`, root).pathname)
    const license = read(`sources/${family}-OFL.txt`).toString()
    for (const weight of weights) {
      const font = openSync(
        new URL(`web/${family.toLowerCase()}-${weight}.woff2`, root).pathname,
      )
      // OpenType IDs 0, 8–12 and 14 must survive subsetting, including translations.
      for (const key of [
        'copyright',
        'manufacturer',
        'designer',
        'description',
        'vendorURL',
        'designerURL',
        'licenseURL',
      ]) {
        if (source.name.records[key])
          assert.deepEqual(
            font.name.records[key],
            source.name.records[key],
            `${family}/${weight}: ${key}`,
          )
      }
      // ID 13 must contain the full local notice, not just an online pointer.
      assert.ok(
        font.name.records.license?.en?.includes(license),
        `${family}/${weight}: full OFL`,
      )
      for (const [language, description] of Object.entries(
        source.name.records.license,
      )) {
        assert.ok(
          font.name.records.license[language]?.includes(description),
          `${family}/${weight}: license ${language}`,
        )
      }
    }
  }
})

test('recorded coverage matches the committed static font inputs', () => {
  for (const entry of json('metadata/coverage.json')) {
    const font = openSync(
      new URL(`sources/Manrope-${entry.weight}.ttf`, root).pathname,
    )
    for (const cp of entry.supported)
      assert.ok(font.hasGlyphForCodePoint(cp), `U+${cp.toString(16)}`)
    for (const cp of entry.missing)
      assert.ok(!font.hasGlyphForCodePoint(cp), `U+${cp.toString(16)}`)
    assert.deepEqual(
      entry.digitAdvances,
      [...'0123456789'].map(
        (ch) => font.glyphForCodePoint(ch.codePointAt(0)).advanceWidth,
      ),
    )
  }
})
test('audio cues match checksums, format and the cue spec', () => {
  const prefix = '../../../firmware/main/features/audio_cues/generated/'
  const spec = json('sources/cues.json')
  const outputs = json('metadata/cues.json')
  assert.equal(new Set(outputs.map((o) => o.file)).size, outputs.length)
  for (const o of outputs) {
    assert.equal(hash(o.file), o.sha256, o.file)
    assert.equal(read(o.file).length, o.bytes, o.file)
  }
  const committed = fs
    .readdirSync(new URL(prefix, root))
    .filter((name) => name.endsWith('.wav'))
  assert.deepEqual(
    committed.sort(),
    Object.keys(spec.cues)
      .map((name) => `${name}.wav`)
      .sort(),
  )
  for (const name of committed) {
    const file = `${prefix}${name}`
    assert.ok(
      outputs.some((o) => o.file === file),
      file,
    )
    const bytes = read(file)
    assert.equal(bytes.toString('ascii', 0, 4), 'RIFF', name)
    assert.equal(bytes.toString('ascii', 8, 12), 'WAVE', name)
    assert.equal(bytes.readUInt16LE(20), 1, `${name}: PCM`)
    assert.equal(bytes.readUInt16LE(22), 1, `${name}: mono`)
    assert.equal(bytes.readUInt32LE(24), spec.sampleRate, name)
    assert.equal(bytes.readUInt16LE(34), 16, `${name}: 16-bit`)
    assert.equal(bytes.toString('ascii', 36, 40), 'data', name)
    const size = bytes.readUInt32LE(40)
    assert.equal(size, bytes.length - 44, name)
    // Short, never louder than the spec's peak, and silent at both ends so the
    // amplifier switching on and off does not click.
    assert.ok(size / 2 / spec.sampleRate < 1, `${name}: under a second`)
    let peak = 0
    for (let i = 44; i < bytes.length; i += 2)
      peak = Math.max(peak, Math.abs(bytes.readInt16LE(i)))
    assert.ok(
      peak > 0 && peak <= Math.round(spec.peak * 32767),
      `${name}: peak ${peak}`,
    )
    assert.equal(bytes.readInt16LE(44), 0, `${name}: starts silent`)
    assert.equal(bytes.readInt16LE(bytes.length - 2), 0, `${name}: ends silent`)
  }
  // Original work: no third-party audio, so no license sidecars to ship.
  assert.ok(!fs.existsSync(new URL(`${prefix}licenses/`, root)))
  assert.match(read(`${prefix}README.md`).toString(), /original to Mosaic/)
})
