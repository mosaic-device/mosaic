// Renders the marketing site's device shots from models/mosaic-device.glb.
//
//   pnpm render-site            every shot, full quality (tens of minutes)
//   pnpm render-site --quick    low samples at half size, to check framing
//   pnpm render-site --only=hero,calm
//
// Needs Blender (set BLENDER to its executable if it isn't in the usual
// place). Draws the device's screens, renders in Blender with Cycles
// (scene.py), then writes AVIFs and journey.json to renders/.
import { spawnSync } from 'node:child_process'
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import sharp from 'sharp'
import { NodeIO } from '@gltf-transform/core'
import { ALL_EXTENSIONS } from '@gltf-transform/extensions'
import { dequantize } from '@gltf-transform/functions'
import { MeshoptDecoder } from 'meshoptimizer'
import { drawScreens } from './screens.mjs'

const here = (p) => fileURLToPath(new URL(p, import.meta.url))
const MODEL = here('../../models/mosaic-device.glb')
const OUT = here('../../renders/')
const args = process.argv.slice(2)
const quick = args.includes('--quick')
const only = args
  .find((a) => a.startsWith('--only='))
  ?.slice('--only='.length)
  .split(',')

function findBlender() {
  const candidates = [
    process.env.BLENDER,
    '/Applications/Blender.app/Contents/MacOS/Blender',
    'blender',
  ].filter(Boolean)
  for (const bin of candidates) {
    const probe = spawnSync(bin, ['--version'], { encoding: 'utf8' })
    if (probe.status === 0) return bin
  }
  throw new Error(
    'Blender not found. Install it, or set BLENDER to its executable.',
  )
}

const blender = findBlender()
const work = fs.mkdtempSync(path.join(os.tmpdir(), 'mosaic-render-'))
const frames = path.join(work, 'frames')

// Blender's glTF importer can't read meshopt compression; unpack it first.
await MeshoptDecoder.ready
const io = new NodeIO()
  .registerExtensions(ALL_EXTENSIONS)
  .registerDependencies({ 'meshopt.decoder': MeshoptDecoder })
const doc = await io.read(MODEL)
await doc.transform(dequantize())
for (const ext of doc.getRoot().listExtensionsUsed())
  if (
    ext.extensionName === 'EXT_meshopt_compression' ||
    ext.extensionName === 'KHR_mesh_quantization'
  )
    ext.dispose()
const model = path.join(work, 'device.glb')
await io.write(model, doc)

const screens = await drawScreens(path.join(work, 'screens'))
const config = JSON.stringify({ model, screens, out: frames, quick, only })
console.log(`Rendering with ${blender}${quick ? ' (quick)' : ''}…`)
const run = spawnSync(
  blender,
  [
    '-b',
    '--factory-startup',
    '--python-exit-code',
    '1',
    '-P',
    here('scene.py'),
    '--',
    config,
  ],
  {
    stdio: ['ignore', 'pipe', 'inherit'],
    encoding: 'utf8',
    maxBuffer: 1 << 28,
  },
)
for (const line of run.stdout.split('\n'))
  if (/^Saved|Error|Traceback/.test(line)) console.log(line)
if (run.status !== 0) throw new Error(`Blender failed (exit ${run.status})`)

// AVIF keeps the alpha the page composites over its own background.
let written = 0
async function encode(dir) {
  for (const name of fs.readdirSync(dir)) {
    const file = path.join(dir, name)
    if (fs.statSync(file).isDirectory()) {
      await encode(file)
      continue
    }
    const rel = path.relative(frames, file)
    const target = path.join(OUT, rel)
    fs.mkdirSync(path.dirname(target), { recursive: true })
    if (name.endsWith('.png')) {
      await sharp(file)
        .avif({ quality: 58, effort: 6 })
        .toFile(target.replace(/\.png$/, '.avif'))
      written++
    } else fs.copyFileSync(file, target)
  }
}
await encode(frames)
fs.rmSync(work, { recursive: true, force: true })
console.log(`Wrote ${written} images to ${OUT}`)
