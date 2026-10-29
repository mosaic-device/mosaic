import assert from 'node:assert/strict'
import { readdir, readFile } from 'node:fs/promises'
import { test } from 'node:test'

const assets = new URL('./dist/assets/', import.meta.url)

test('production redistributes exact source OFL notices for both font families', async () => {
  for (const name of ['Manrope-OFL.txt', 'Outfit-OFL.txt']) {
    const source = await readFile(
      new URL(`../../packages/assets/licenses/${name}`, import.meta.url),
    )
    const publicCopy = await readFile(
      new URL(`./public/licenses/${name}`, import.meta.url),
    )
    const productionCopy = await readFile(
      new URL(`./dist/licenses/${name}`, import.meta.url),
    )
    assert.deepEqual(
      publicCopy,
      source,
      `${name}: public notice differs from source`,
    )
    assert.deepEqual(
      productionCopy,
      source,
      `${name}: production notice missing or changed`,
    )
  }
})

test('production excludes the development preview', async () => {
  const files = await readdir(assets)
  const scripts = files.filter((name) => name.endsWith('.js'))
  assert.ok(scripts.length > 0, 'Build admin before running this test')
  const runtime = (
    await Promise.all(
      scripts.map((name) => readFile(new URL(name, assets), 'utf8')),
    )
  ).join('\n')
  assert.ok(
    !runtime.includes('/dev/components'),
    'Development preview route leaked into production',
  )
  assert.ok(
    !runtime.includes('Component foundation'),
    'Development preview content leaked into production',
  )
  assert.ok(
    !files.some((name) => /^ComponentPreview(?:-|\.)/.test(name)),
    'Development preview chunk leaked into production',
  )
})

test('production index loads assets from the site root so deep links work', async () => {
  const html = await readFile(
    new URL('./dist/index.html', import.meta.url),
    'utf8',
  )
  assert.match(html, /src="\/assets\/[^"]+\.js"/)
  assert.doesNotMatch(html, /(?:src|href)="\.\/assets\//)
})

test('production ships self-hosted fonts without external CSS requests', async () => {
  const files = await readdir(assets)
  for (const font of ['manrope-400-', 'manrope-500-', 'outfit-500-']) {
    assert.ok(
      files.some((name) => name.startsWith(font) && name.endsWith('.woff2')),
      `Missing ${font}`,
    )
  }
  const css = (
    await Promise.all(
      files
        .filter((name) => name.endsWith('.css'))
        .map((name) => readFile(new URL(name, assets), 'utf8')),
    )
  ).join('\n')
  assert.ok(
    !/(?:url\(\s*['"]?|@import\s*['"])(?:https?:)?\/\//.test(css),
    'CSS must not fetch external fonts or styles',
  )
})
