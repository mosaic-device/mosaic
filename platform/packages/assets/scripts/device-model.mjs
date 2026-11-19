// Builds models/mosaic-device.glb, the device model for the web, from the
// Onshape assembly (STEP) and the KiCad carrier board export (GLB).
//
// The STEP gives every part's shape and position. Its carrier PCB is only a
// placeholder, so it is swapped for the KiCad board, which has the real solder
// mask, silkscreen and components, aligned onto the placeholder.
//
// Output: glTF, Y up, metres, the device's front facing +Z, centred on X and Z
// with its base at Y = 0. Each part is a top-level node named in PARTS, with its
// origin at its own centre so it can be moved or turned in place.
import fs from 'node:fs'
import { fileURLToPath } from 'node:url'
import occt from 'occt-import-js'
import { Document, NodeIO } from '@gltf-transform/core'
import { ALL_EXTENSIONS } from '@gltf-transform/extensions'
import {
  dedup,
  getBounds,
  meshopt,
  prune,
  weld,
} from '@gltf-transform/functions'
import { MeshoptEncoder } from 'meshoptimizer'

const repo = (path) =>
  fileURLToPath(new URL(`../../../../${path}`, import.meta.url))
const STEP = repo('hardware/cad/mosaic-device-cad.step')
const KICAD = repo('hardware/pcb/mosaic-carrier/mosaic-carrier.glb')
const OUT = fileURLToPath(
  new URL('../models/mosaic-device.glb', import.meta.url),
)

// The device's front in the Onshape assembly's coordinates.
const CAD_FRONT = [0, -1, 0]
const CAD_PCB = 'mosaic-carrier_PCB'

// Materials the web model uses instead of the CAD colours. The site can find
// them by name, for example to put a screenshot on `screen`.
const MATERIALS = {
  case: { color: [0.8, 0.79, 0.76], roughness: 0.65 },
  aluminum: { color: [0.91, 0.92, 0.92], metallic: 1, roughness: 0.35 },
  acrylic: { color: [0.02, 0.02, 0.02], alpha: 0.25, roughness: 0.05 },
  diffuser: { color: [0.92, 0.92, 0.92], roughness: 0.9 },
  screen: { color: [0.01, 0.01, 0.01], roughness: 0.2 },
}

// CAD part name → node name, and the material to use instead of the CAD colours.
const PARTS = {
  Outer: { node: 'case-outer', material: 'case' },
  Inner: { node: 'case-inner', material: 'case' },
  Acrylic: { node: 'acrylic', material: 'acrylic' },
  Dial: { node: 'dial', material: 'aluminum' },
  'Ring Diffuser': { node: 'ring-diffuser', material: 'diffuser' },
  'Led Ring': { node: 'led-ring' },
  'ESP32-P4-WIFI6-Touch-LCD-5': { node: 'display-board', screen: true },
  'KY-040 Rotary Encoder Module': { node: 'encoder' },
  'BH 1750': { node: 'light-sensor' },
  Speaker: { node: 'speaker' },
  'Female USB C': { node: 'usb-c' },
}

// ---------------------------------------------------------------- vector math
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
const cross = (a, b) => [
  a[1] * b[2] - a[2] * b[1],
  a[2] * b[0] - a[0] * b[2],
  a[0] * b[1] - a[1] * b[0],
]
const add = (a, b) => a.map((v, i) => v + b[i])
const sub = (a, b) => a.map((v, i) => v - b[i])
const scale = (a, s) => a.map((v) => v * s)
const norm = (a) => scale(a, 1 / Math.hypot(...a))
// A rotation as three column vectors: where x, y and z go.
const rotate = ([cx, cy, cz], p) =>
  add(add(scale(cx, p[0]), scale(cy, p[1])), scale(cz, p[2]))
const mat4Point = (m, p) => [
  m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12],
  m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13],
  m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14],
]

// CAD (millimetres, Z up, front CAD_FRONT) → glTF (metres, Y up, front +Z).
const toGltf = (p) => [p[0] / 1000, p[2] / 1000, -p[1] / 1000]

const points = (array) => {
  const out = []
  for (let i = 0; i < array.length; i += 3)
    out.push([array[i], array[i + 1], array[i + 2]])
  return out
}

function triangles(mesh) {
  const p = mesh.attributes.position.array
  const index = mesh.index.array
  const out = []
  for (let t = 0; t < index.length; t += 3) {
    const [a, b, c] = [0, 1, 2].map((k) => {
      const i = index[t + k] * 3
      return [p[i], p[i + 1], p[i + 2]]
    })
    const n = cross(sub(b, a), sub(c, a))
    const area2 = Math.hypot(...n)
    if (area2 > 0) out.push({ a, n: scale(n, 1 / area2), area: area2 / 2 })
  }
  return out
}

// ---------------------------------------------------------- read the assembly
const step = (await occt()).ReadStepFile(
  new Uint8Array(fs.readFileSync(STEP)),
  {
    linearUnit: 'millimeter',
    linearDeflectionType: 'bounding_box_ratio',
    linearDeflection: 0.001,
    angularDeflection: 0.5,
  },
)
if (!step.success) throw new Error(`could not read ${STEP}`)
const cadParts = new Map(step.meshes.map((m) => [m.name, m]))
for (const name of [...Object.keys(PARTS), CAD_PCB])
  if (!cadParts.has(name))
    throw new Error(`the STEP has no part named "${name}"`)
for (const name of cadParts.keys())
  if (!PARTS[name] && name !== CAD_PCB)
    console.warn(`warning: "${name}" isn't in PARTS; it keeps its CAD colours`)

// ---------------------------------------- align the KiCad board onto the CAD one
// The CAD placeholder is a bare board, so the fit uses the KiCad board body
// alone: find the CAD board's plane, try the four ways the KiCad board could lie
// in it, and keep the one whose outline and holes match. The hole pattern isn't
// symmetric, so exactly one fits.
const io = new NodeIO()
  .registerExtensions(ALL_EXTENSIONS)
  .registerDependencies({ 'meshopt.encoder': MeshoptEncoder })
const doc = await io.read(KICAD)
const kicadScene = doc.getRoot().listScenes()[0]

const pcbPoints = points(cadParts.get(CAD_PCB).attributes.position.array)
const mm = (a) => a.map((v) => v * 1000)
// KiCad's board body is the node as wide as the board and about 1.6 mm thick.
const kicadOutline = getBounds(kicadScene)
const outlineSize = sub(kicadOutline.max, kicadOutline.min)
let body = null
kicadScene.traverse((node) => {
  if (!node.getMesh()) return
  const b = getBounds(node)
  const size = sub(b.max, b.min)
  if (
    Math.abs(size[0] - outlineSize[0]) < 1e-4 &&
    Math.abs(size[2] - outlineSize[2]) < 1e-4 &&
    size[1] > 0.001 &&
    size[1] < 0.002
  )
    body = node
})
if (!body) throw new Error('could not find the board body in the KiCad export')
const bodyPoints = []
const bodyMatrix = body.getWorldMatrix()
for (const prim of body.getMesh().listPrimitives()) {
  const pos = prim.getAttribute('POSITION')
  for (let i = 0; i < pos.getCount(); i++)
    bodyPoints.push(mm(mat4Point(bodyMatrix, pos.getElement(i, []))))
}
const bodyBounds = getBounds(body)
const kicadCentre = mm(
  bodyBounds.min.map((v, i) => (v + bodyBounds.max[i]) / 2),
)

// The CAD board's faces are its largest area facing one way.
const facing = new Map()
for (const t of triangles(cadParts.get(CAD_PCB))) {
  const key = t.n.map((v) => Math.abs(v).toFixed(2)).join()
  const entry = facing.get(key) ?? { area: 0, sum: [0, 0, 0], first: t.n }
  entry.area += t.area
  entry.sum = add(
    entry.sum,
    scale(t.n, t.area * Math.sign(dot(t.n, entry.first))),
  )
  facing.set(key, entry)
}
let normal = norm([...facing.values()].sort((a, b) => b.area - a.area)[0].sum)
if (dot(normal, CAD_FRONT) > 0) normal = scale(normal, -1) // points to the back
const across = norm(sub([1, 0, 0], scale(normal, normal[0]))) // board's long edge
const up = cross(across, normal)
const middle = (axis) => {
  const v = pcbPoints.map((p) => dot(p, axis))
  return (Math.min(...v) + Math.max(...v)) / 2
}
const cadCentre = [across, up, normal].reduce(
  (sum, axis) => add(sum, scale(axis, middle(axis))),
  [0, 0, 0],
)

const nearest = (from, to) => {
  let total = 0
  for (const q of from) {
    let best = Infinity
    for (const r of to) {
      const d = (q[0] - r[0]) ** 2 + (q[1] - r[1]) ** 2 + (q[2] - r[2]) ** 2
      if (d < best) best = d
    }
    total += Math.sqrt(best)
  }
  return total / from.length
}
const fits = []
for (const sx of [1, -1])
  for (const sy of [1, -1]) {
    const x = scale(across, sx)
    const y = scale(normal, sy)
    const rotation = [x, y, cross(x, y)]
    const offset = sub(cadCentre, rotate(rotation, kicadCentre))
    const placed = bodyPoints.map((p) => add(rotate(rotation, p), offset))
    const error = (nearest(placed, pcbPoints) + nearest(pcbPoints, placed)) / 2
    fits.push({ rotation, offset, error })
  }
fits.sort((a, b) => a.error - b.error)
const fit = fits[0]
console.log(
  `carrier board fit: ${fit.error.toFixed(3)} mm average (next best ${fits[1].error.toFixed(3)} mm)`,
)
if (fit.error > 0.2 || fits[1].error < 1)
  throw new Error('the KiCad board does not line up with the CAD placeholder')

// ------------------------------------------------------ place everything in glTF
// Find where the base and centre end up, so the model can sit at the origin.
let lo = [Infinity, Infinity, Infinity]
let hi = [-Infinity, -Infinity, -Infinity]
for (const [name, mesh] of cadParts) {
  if (name === CAD_PCB) continue
  for (const p of points(mesh.attributes.position.array)) {
    const g = toGltf(p)
    lo = lo.map((v, i) => Math.min(v, g[i]))
    hi = hi.map((v, i) => Math.max(v, g[i]))
  }
}
const origin = [(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2]
const place = (p) => sub(toGltf(p), origin)

const scene = doc.createScene('mosaic-device')
doc.getRoot().setDefaultScene(scene)
const buffer = doc.getRoot().listBuffers()[0]

const materials = new Map()
function material(name, spec) {
  if (materials.has(name)) return materials.get(name)
  const m = doc
    .createMaterial(name)
    .setBaseColorFactor([...spec.color, spec.alpha ?? 1])
    .setMetallicFactor(spec.metallic ?? 0)
    .setRoughnessFactor(spec.roughness ?? 0.5)
  if (spec.alpha !== undefined) m.setAlphaMode('BLEND')
  materials.set(name, m)
  return m
}
const cadMaterial = (color) => {
  const hex = color
    .map((v) =>
      Math.round(v * 255)
        .toString(16)
        .padStart(2, '0'),
    )
    .join('')
  return material(`cad-${hex}`, { color })
}

function primitive(positions, normals, uvs, mat) {
  const prim = doc
    .createPrimitive()
    .setMaterial(mat)
    .setAttribute(
      'POSITION',
      doc
        .createAccessor()
        .setType('VEC3')
        .setArray(new Float32Array(positions))
        .setBuffer(buffer),
    )
    .setAttribute(
      'NORMAL',
      doc
        .createAccessor()
        .setType('VEC3')
        .setArray(new Float32Array(normals))
        .setBuffer(buffer),
    )
  if (uvs)
    prim.setAttribute(
      'TEXCOORD_0',
      doc
        .createAccessor()
        .setType('VEC2')
        .setArray(new Float32Array(uvs))
        .setBuffer(buffer),
    )
  return prim
}

// Splits a CAD part into one primitive per material, plus the screen face if
// asked. Vertices are relative to `centre`.
function buildPart(cad, spec, centre) {
  const p = cad.attributes.position.array
  const n = cad.attributes.normal.array
  const index = cad.index.array
  const faces = cad.brep_faces.length
    ? cad.brep_faces
    : [{ first: 0, last: index.length / 3 - 1, color: null }]
  const groups = new Map()
  const vertex = (group, v) => {
    const at = sub(place([p[v * 3], p[v * 3 + 1], p[v * 3 + 2]]), centre)
    group.positions.push(...at)
    group.normals.push(
      ...toGltf([n[v * 3], n[v * 3 + 1], n[v * 3 + 2]]).map((c) => c * 1000),
    )
  }

  // The screen is the largest single face on the display board facing front.
  let screenFace = -1
  if (spec.screen) {
    let largest = 0
    faces.forEach((face, f) => {
      let area = 0
      let facing = [0, 0, 0]
      for (let t = face.first; t <= face.last; t++) {
        const [a, b, c] = [0, 1, 2].map((k) => {
          const i = index[t * 3 + k] * 3
          return [p[i], p[i + 1], p[i + 2]]
        })
        const cr = cross(sub(b, a), sub(c, a))
        area += Math.hypot(...cr) / 2
        facing = add(facing, cr)
      }
      if (area > largest && dot(norm(facing), norm(scale(normal, -1))) > 0.99) {
        largest = area
        screenFace = f
      }
    })
    if (screenFace < 0)
      throw new Error('no front-facing screen face on the display board')
  }

  faces.forEach((face, f) => {
    const isScreen = f === screenFace
    const mat = isScreen
      ? material('screen', MATERIALS.screen)
      : spec.material
        ? material(spec.material, MATERIALS[spec.material])
        : cadMaterial(face.color ?? cad.color ?? [0.5, 0.5, 0.5])
    const key = isScreen ? 'screen' : mat.getName()
    if (!groups.has(key))
      groups.set(key, { mat, positions: [], normals: [], screen: isScreen })
    const group = groups.get(key)
    for (let t = face.first; t <= face.last; t++)
      for (let k = 0; k < 3; k++) vertex(group, index[t * 3 + k])
  })

  const mesh = doc.createMesh(spec.node)
  for (const group of groups.values()) {
    let uvs = null
    if (group.screen) {
      // Map the face to 0–1, left to right and top to bottom as the viewer sees it.
      const right = toGltf(across).map((v) => v * 1000)
      const top = toGltf(up).map((v) => v * 1000)
      const all = points(group.positions)
      const u = all.map((q) => dot(q, right))
      const v = all.map((q) => dot(q, top))
      const [u0, u1, v0, v1] = [
        Math.min(...u),
        Math.max(...u),
        Math.min(...v),
        Math.max(...v),
      ]
      uvs = all.flatMap((_, i) => [
        (u[i] - u0) / (u1 - u0),
        (v1 - v[i]) / (v1 - v0),
      ])
    }
    mesh.addPrimitive(primitive(group.positions, group.normals, uvs, group.mat))
  }
  return mesh
}

for (const [name, spec] of Object.entries(PARTS)) {
  const cad = cadParts.get(name)
  const placed = points(cad.attributes.position.array).map(place)
  const centre = [0, 1, 2].map(
    (i) =>
      (Math.min(...placed.map((q) => q[i])) +
        Math.max(...placed.map((q) => q[i]))) /
      2,
  )
  scene.addChild(
    doc
      .createNode(spec.node)
      .setTranslation(centre)
      .setMesh(buildPart(cad, spec, centre)),
  )
}

// The KiCad board: rotate it into the CAD frame, then into glTF. KiCad's
// points are in metres; the fit's offset is in CAD millimetres.
const gltfAxes = [
  toGltf([1000, 0, 0]),
  toGltf([0, 1000, 0]),
  toGltf([0, 0, 1000]),
]
const [cx, cy, cz] = fit.rotation.map((axis) => rotate(gltfAxes, axis))
const board = doc.createNode('carrier-pcb-geometry')
for (const top of kicadScene.listChildren()) {
  kicadScene.removeChild(top)
  board.addChild(top)
}
kicadScene.dispose()
for (const m of doc.getRoot().listMaterials())
  if (!materials.has(m.getName()))
    m.setAlphaMode('OPAQUE').setName(`pcb-${m.getName()}`)
// Give the board a parent at its own centre, like the other parts.
const at = place(fit.offset)
board.setMatrix([...cx, 0, ...cy, 0, ...cz, 0, ...at, 1])
const bounds = getBounds(board)
const boardCentre = [0, 1, 2].map((i) => (bounds.min[i] + bounds.max[i]) / 2)
board.setMatrix([...cx, 0, ...cy, 0, ...cz, 0, ...sub(at, boardCentre), 1])
scene.addChild(
  doc.createNode('carrier-pcb').setTranslation(boardCentre).addChild(board),
)

// ---------------------------------------------------------------- write it out
await doc.transform(
  dedup(),
  weld(),
  // Keep the screen's UVs although no texture uses them until the site adds one.
  prune({ keepAttributes: true }),
  meshopt({ encoder: MeshoptEncoder, level: 'medium' }),
)
fs.mkdirSync(new URL('../models/', import.meta.url), { recursive: true })
await io.write(OUT, doc)

const size = fs.statSync(OUT).size
const b = getBounds(scene)
const dims = sub(b.max, b.min)
  .map((v) => (v * 1000).toFixed(0))
  .join(' × ')
console.log(`wrote ${OUT} (${(size / 1024).toFixed(0)} KiB, ${dims} mm)`)
