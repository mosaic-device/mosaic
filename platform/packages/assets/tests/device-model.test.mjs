import { test } from 'node:test'
import assert from 'node:assert/strict'
import fs from 'node:fs'
import { fileURLToPath } from 'node:url'
import { NodeIO } from '@gltf-transform/core'
import { ALL_EXTENSIONS } from '@gltf-transform/extensions'
import { getBounds } from '@gltf-transform/functions'
import { MeshoptDecoder } from 'meshoptimizer'

const file = fileURLToPath(
  new URL('../models/mosaic-device.glb', import.meta.url),
)
await MeshoptDecoder.ready
const doc = await new NodeIO()
  .registerExtensions(ALL_EXTENSIONS)
  .registerDependencies({ 'meshopt.decoder': MeshoptDecoder })
  .read(file)
const scene = doc.getRoot().getDefaultScene()

test('the device model has every part the site moves, by name', () => {
  assert.deepEqual(
    scene
      .listChildren()
      .map((n) => n.getName())
      .sort(),
    [
      'acrylic',
      'carrier-pcb',
      'case-inner',
      'case-outer',
      'dial',
      'display-board',
      'encoder',
      'led-ring',
      'light-sensor',
      'ring-diffuser',
      'speaker',
      'usb-c',
    ],
  )
  for (const node of scene.listChildren()) {
    const b = getBounds(node)
    assert.ok(b.max[0] > b.min[0], `${node.getName()} has geometry`)
  }
})

test('the device sits on the origin, front +Z, at its real size', () => {
  const { min, max } = getBounds(scene)
  assert.ok(Math.abs(min[1]) < 1e-4, 'base at y = 0')
  assert.ok(Math.abs(min[0] + max[0]) < 1e-4, 'centred on x')
  assert.ok(Math.abs(min[2] + max[2]) < 1e-4, 'centred on z')
  const size = [0, 1, 2].map((i) => max[i] - min[i])
  assert.ok(size[0] > 0.15 && size[0] < 0.25, `width ${size[0]} m`)
  // The dial is the part furthest forward.
  const dial = getBounds(
    scene.listChildren().find((n) => n.getName() === 'dial'),
  )
  assert.ok(Math.abs(dial.max[2] - max[2]) < 1e-4, 'dial faces +Z')
})

test('the screen is a front-facing face with UVs for a screenshot', () => {
  const prims = doc
    .getRoot()
    .listMeshes()
    .flatMap((m) => m.listPrimitives())
    .filter((p) => p.getMaterial()?.getName() === 'screen')
  assert.equal(prims.length, 1)
  const uv = prims[0].getAttribute('TEXCOORD_0')
  assert.ok(uv, 'screen has TEXCOORD_0')
  const values = Array.from({ length: uv.getCount() }, (_, i) =>
    uv.getElement(i, []),
  )
  for (const axis of [0, 1]) {
    assert.ok(Math.min(...values.map((v) => v[axis])) < 0.01)
    assert.ok(Math.max(...values.map((v) => v[axis])) > 0.99)
  }
  const normal = prims[0].getAttribute('NORMAL').getElement(0, [])
  assert.ok(normal[2] > 0.9, 'screen faces +Z')
})

test('the device model stays small enough to load on a web page', () => {
  assert.ok(fs.statSync(file).size < 4 * 1024 * 1024)
})
