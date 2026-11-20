import { test } from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import { fileURLToPath } from 'node:url'
import sharp from 'sharp'

const file = (name) =>
  fileURLToPath(new URL(`../renders/${name}`, import.meta.url))
const journey = JSON.parse(fs.readFileSync(file('journey.json'), 'utf8'))
const pad = (n) => String(n).padStart(2, '0')

test('every still the site uses is rendered, with transparency', async () => {
  for (const name of [
    'hero',
    'waits-dark',
    'waits-ring',
    'waits-review',
    'calm',
  ]) {
    const meta = await sharp(file(`${name}.avif`)).metadata()
    assert.ok(meta.hasAlpha, `${name} has an alpha channel`)
    assert.ok(meta.width >= 1400, `${name} is a full render, not --quick`)
  }
})

test('the journey has every frame and an anchor set for each', async () => {
  assert.ok(journey.a > 0 && journey.b > journey.bMove && journey.bMove > 0)
  assert.equal(journey.anchors.a.length, journey.a)
  assert.equal(journey.anchors.b.length, journey.b)
  const frames = [
    ...Array.from({ length: journey.a }, (_, i) => `a-${pad(i)}`),
    'a-ring',
    ...Array.from({ length: journey.b }, (_, i) => `b-${pad(i)}`),
  ]
  for (const name of frames)
    assert.ok(fs.existsSync(file(`journey/${name}.avif`)), name)
  const meta = await sharp(file('journey/b-00.avif')).metadata()
  assert.deepEqual([meta.width, meta.height], journey.size, 'full-size frames')
})

test('labelled parts have anchors in the settled frame', () => {
  const settled = journey.anchors.b.at(-1)
  for (const name of [
    'dial',
    'acrylic',
    'display-board',
    'carrier-pcb',
    'case-outer',
  ]) {
    const [x, y] = settled[name]
    assert.ok(x > 0 && x < 1 && y > 0 && y < 1, `${name} is in frame`)
  }
})
