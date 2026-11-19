// S5's world in simple shapes: the plate that is yours, your server, your laptop
// and your accounts floating outside. Laid out in the design's isometric units
// (one unit = the design's drawing pixel; Mosaic is 150 units wide), with A
// running back-left to front-right and B back-right to front-left.
import {
  BoxGeometry,
  BufferGeometry,
  Group,
  LineDashedMaterial,
  LineLoop,
  Mesh,
  MeshStandardMaterial,
  Object3D,
  SphereGeometry,
  Vector3,
} from 'three'

/** Metres per design unit: Mosaic's real width over its drawn width. */
export const UNIT = 0.19 / 150

const matte = (color: string) =>
  new MeshStandardMaterial({ color, roughness: 0.85, metalness: 0 })

/** A point on the plate, in design units, as a position in the diorama. */
export const at = (a: number, h: number, b: number) =>
  new Vector3(a * UNIT, h * UNIT, b * UNIT)

function box(
  size: [number, number, number],
  centre: [number, number, number],
  material: MeshStandardMaterial,
) {
  const mesh = new Mesh(
    new BoxGeometry(size[0] * UNIT, size[1] * UNIT, size[2] * UNIT),
    material,
  )
  mesh.position.copy(at(...centre))
  return mesh
}

export interface Diorama {
  root: Group
  /** Parts that rise onto the plate as the section plays. */
  risers: Object3D[]
  accounts: Object3D[]
  /** Where Mosaic stands, its front facing +B. */
  mosaic: Vector3
  anchors: Record<string, Vector3>
}

export function buildDiorama(): Diorama {
  const root = new Group()

  // The plate, 420 × 320 units, its top at height 0.
  const plate = box([420, 12, 320], [210, -6, 160], matte('#0D0E0E'))
  root.add(plate)
  const outline = new BufferGeometry().setFromPoints([
    at(0, 0.5, 0),
    at(420, 0.5, 0),
    at(420, 0.5, 320),
    at(0, 0.5, 320),
  ])
  const dashed = new LineLoop(
    outline,
    new LineDashedMaterial({
      color: '#7540B5',
      dashSize: 5 * UNIT,
      gapSize: 6 * UNIT,
    }),
  )
  dashed.computeLineDistances()
  root.add(dashed)

  // Your server: a 110 × 64 × 110 box with vents and a status light.
  const server = new Group()
  server.add(box([110, 64, 110], [95, 32, 115], matte('#18191B')))
  for (const b of [80, 100, 120])
    server.add(box([70, 1.5, 3], [95, 64.6, b], matte('#303335')))
  const light = new Mesh(
    new SphereGeometry(2.5 * UNIT, 12, 8),
    new MeshStandardMaterial({
      color: '#92979B',
      emissive: '#92979B',
      emissiveIntensity: 0.4,
    }),
  )
  light.position.copy(at(140, 56, 171))
  server.add(light)
  root.add(server)

  // Your laptop: a thin base and a screen standing at its back edge.
  const laptop = new Group()
  laptop.add(box([110, 4, 60], [95, 2, 260], matte('#18191B')))
  const lid = new Group()
  lid.position.copy(at(95, 4, 231))
  lid.rotation.x = -0.18
  const panel = box([110, 58, 3], [0, 29, 0], matte('#111213'))
  lid.add(panel)
  const display = box(
    [96, 46, 0.5],
    [0, 30, 1.8],
    new MeshStandardMaterial({
      color: '#070808',
      roughness: 0.3,
      emissive: '#1a1b1d',
    }),
  )
  lid.add(display)
  laptop.add(lid)
  root.add(laptop)

  // Your accounts: four small tiles floating outside the plate.
  const accounts: Object3D[] = []
  for (const [a, b] of [
    [-65, 125],
    [-35, 125],
    [-50, 155],
    [-20, 155],
  ] as const) {
    const tile = box([22, 6, 22], [a, 120, b], matte('#222426'))
    accounts.push(tile)
    root.add(tile)
  }

  return {
    root,
    risers: [server, laptop],
    accounts,
    mosaic: at(305, 0, 180),
    anchors: {
      server: at(150, 64, 60),
      serverFront: at(95, 64, 170),
      laptop: at(95, 0, 290),
      accounts: at(-42, 135, 140),
      yours: at(0, 0, 0),
    },
  }
}
