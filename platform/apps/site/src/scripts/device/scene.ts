// One fixed 3D canvas for the whole homepage. Each section marks where the
// device belongs with a slot element ([data-slot]); the scene fits the real
// device model into that slot.
//
// With motion, one device travels: scroll moves it from slot to slot, so every
// handoff is the same object moving. Without motion (phones, reduced motion)
// each slot gets its own still copy.
import {
  ACESFilmicToneMapping,
  Box3,
  Color,
  DirectionalLight,
  Euler,
  Group,
  MathUtils,
  Mesh,
  MeshStandardMaterial,
  Object3D,
  PerspectiveCamera,
  PMREMGenerator,
  Quaternion,
  Scene,
  SRGBColorSpace,
  Vector3,
  WebGLRenderer,
} from 'three'
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader.js'
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js'
import { MeshoptDecoder } from 'three/examples/jsm/libs/meshopt_decoder.module.js'
import modelUrl from '@mosaic/assets/models/mosaic-device.glb?url'
import { ScreenTexture } from './screen'
import { buildDiorama, UNIT, type Diorama } from './diorama'

// How high each part lifts when the device comes apart, in layers.
const LAYERS: Record<string, number> = {
  dial: 5,
  acrylic: 4,
  'ring-diffuser': 4,
  'display-board': 3,
  'led-ring': 3,
  'carrier-pcb': 2,
  encoder: 2,
  'light-sensor': 2,
  'case-inner': 1,
  speaker: 1,
  'usb-c': 1,
  'case-outer': 0,
}
const LAYER_GAP = 0.032
const RING_BLUE = new Color(0.12, 0.39, 1)

const clamp = (v: number, lo = 0, hi = 1) => Math.min(hi, Math.max(lo, v))
const smooth = (v: number) => {
  const t = clamp(v)
  return t * t * (3 - 2 * t)
}
const range = (v: number, a: number, b: number) => smooth((v - a) / (b - a))

interface Pose {
  position: Vector3
  quaternion: Quaternion
  scale: number
  explode: number
  /** 0 review request, 1 calendar. */
  screen: number
  /** 0 dark, 1 blue. */
  ring: number
  /** 0 the whole device, 1 only the knob. */
  solo: number
}

function mixPose(a: Pose, b: Pose, t: number): Pose {
  return {
    position: a.position.clone().lerp(b.position, t),
    quaternion: a.quaternion.clone().slerp(b.quaternion, t),
    scale: Math.exp(MathUtils.lerp(Math.log(a.scale), Math.log(b.scale), t)),
    explode: MathUtils.lerp(a.explode, b.explode, t),
    screen: MathUtils.lerp(a.screen, b.screen, t),
    ring: MathUtils.lerp(a.ring, b.ring, t),
    solo: MathUtils.lerp(a.solo, b.solo, t),
  }
}

/** One copy of the device: the model, its own screen and ring, its parts. */
class Device {
  readonly root = new Group()
  readonly screen = new ScreenTexture()
  private readonly ring: MeshStandardMaterial
  private readonly parts: { node: Object3D; home: Vector3; layer: number }[] =
    []
  /** Materials of everything but the knob, which fade out for S3. */
  private readonly rest: MeshStandardMaterial[] = []
  private solo = -1

  constructor(
    source: Object3D,
    private readonly up: Vector3,
  ) {
    const model = source.clone(true)
    let ring: MeshStandardMaterial | undefined
    const knob = new Set(['dial', 'ring-diffuser'])
    model.traverse((o) => {
      if (!(o instanceof Mesh)) return
      const m = o.material as MeshStandardMaterial
      let part: Object3D | null = o
      while (part && part.parent !== model) part = part.parent
      if (part && !knob.has(part.name) && m.name !== 'screen') {
        const own = m.clone()
        o.material = own
        this.rest.push(own)
        return
      }
      if (m.name === 'screen') {
        const own = m.clone()
        own.emissiveMap = this.screen.texture
        own.emissive = new Color(1, 1, 1)
        own.emissiveIntensity = 1
        o.material = own
        this.rest.push(own)
      } else if (m.name === 'diffuser') {
        ring ??= m.clone()
        o.material = ring
      }
    })
    this.ring = ring ?? new MeshStandardMaterial()
    for (const node of model.children)
      this.parts.push({
        node,
        home: node.position.clone(),
        layer: LAYERS[node.name] ?? 0,
      })
    this.root.add(model)
  }

  part(name: string) {
    return this.parts.find((p) => p.node.name === name)?.node
  }

  apply(pose: Pose) {
    this.root.position.copy(pose.position)
    this.root.quaternion.copy(pose.quaternion)
    this.root.scale.setScalar(pose.scale)
    const e = smooth(pose.explode)
    for (const { node, home, layer } of this.parts)
      node.position.copy(home).addScaledVector(this.up, layer * LAYER_GAP * e)
    this.screen.set(pose.screen)
    this.ring.emissive.copy(RING_BLUE).multiplyScalar(pose.ring * 1.6)
    const solo = Math.round(pose.solo * 50) / 50
    if (solo !== this.solo) {
      this.solo = solo
      for (const m of this.rest) {
        const fading = solo > 0
        if (m.transparent !== fading || m.userData.baseOpacity === undefined) {
          m.userData.baseOpacity ??= m.opacity
          m.userData.baseTransparent ??= m.transparent
          m.transparent = fading || (m.userData.baseTransparent as boolean)
          m.needsUpdate = true
        }
        m.opacity = (m.userData.baseOpacity as number) * (1 - solo)
        m.depthWrite = !fading
        m.visible = solo < 0.99
      }
    }
  }
}

export async function mountScene(canvas: HTMLCanvasElement) {
  const motion = matchMedia(
    '(min-width: 900px) and (prefers-reduced-motion: no-preference)',
  )
  const renderer = new WebGLRenderer({ canvas, antialias: true, alpha: true })
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2))
  renderer.outputColorSpace = SRGBColorSpace
  renderer.toneMapping = ACESFilmicToneMapping
  renderer.toneMappingExposure = 1.05

  const scene = new Scene()
  scene.environment = new PMREMGenerator(renderer).fromScene(
    new RoomEnvironment(),
    0.04,
  ).texture
  scene.environmentIntensity = 0.5
  const key = new DirectionalLight(0xffffff, 1.3)
  key.position.set(-0.5, 1, 0.8)
  scene.add(key)
  const warm = new DirectionalLight(0xffa050, 0.7)
  warm.position.set(0.4, -0.6, 0.5)
  scene.add(warm)

  await document.fonts.ready
  const gltf = await new GLTFLoader()
    .setMeshoptDecoder(MeshoptDecoder)
    .loadAsync(modelUrl)
  const model = gltf.scene
  // The design's device is dark; the generator's case colour is a placeholder.
  model.traverse((o) => {
    if (!(o instanceof Mesh)) return
    const m = o.material as MeshStandardMaterial
    if (m.name === 'case') {
      m.color = new Color('#2a2c2e')
      m.roughness = 0.7
    } else if (m.name === 'aluminum') {
      // A dark anodised knob, as drawn.
      m.color = new Color('#4a4d50')
      m.roughness = 0.38
    }
  })

  // The screen's normal is the device's front.
  let front = new Vector3(0, 0, 1)
  model.traverse((o) => {
    if (
      o instanceof Mesh &&
      (o.material as MeshStandardMaterial).name === 'screen'
    ) {
      const n = o.geometry.getAttribute('normal')
      front = new Vector3(n.getX(0), n.getY(0), n.getZ(0)).normalize()
    }
  })

  // Boxes in the model's own frame that each slot frames.
  const whole = new Box3().setFromObject(model)
  const dialNode = model.getObjectByName('dial')!
  const dial = new Box3().setFromObject(dialNode)
  const apart = (() => {
    const box = new Box3()
    for (const node of model.children) {
      const b = new Box3().setFromObject(node)
      b.translate(
        front.clone().multiplyScalar((LAYERS[node.name] ?? 0) * LAYER_GAP),
      )
      box.union(b)
    }
    return box
  })()

  // Orientations. The device turns its screen to face the viewer, a little to
  // one side; S5 and S6 are isometric.
  const face = new Quaternion().setFromUnitVectors(front, new Vector3(0, 0, 1))
  const turn = (yaw: number, pitch: number) =>
    new Quaternion()
      .setFromEuler(
        new Euler(MathUtils.degToRad(pitch), MathUtils.degToRad(yaw), 0, 'XYZ'),
      )
      .multiply(face)
  const qHero = turn(-14, 6)
  const qWaits = turn(-10, 4)
  const qKnob = turn(-8, 2)
  const qIso = new Quaternion().setFromEuler(
    new Euler(MathUtils.degToRad(35.264), MathUtils.degToRad(-45), 0, 'XYZ'),
  )
  const qFlat = qIso
    .clone()
    .multiply(new Quaternion().setFromUnitVectors(front, new Vector3(0, 1, 0)))

  // The camera looks down -Z at a plane where one world unit is `k` pixels'
  // worth; a narrow field of view keeps it close to the flat drawings.
  const camera = new PerspectiveCamera(14, 1, 0.1, 100)
  camera.position.set(0, 0, 10)
  camera.updateMatrixWorld()
  let k = 1
  const toWorld = (x: number, y: number) =>
    new Vector3((x - innerWidth / 2) * k, -(y - innerHeight / 2) * k, 0)

  /**
   * Fits `box` (model frame), turned by `q`, into a page rectangle: inside it,
   * or across its width with `byWidth`.
   */
  function fit(rect: DOMRect, q: Quaternion, box: Box3, byWidth = false): Pose {
    const lo = new Vector3(Infinity, Infinity, Infinity)
    const hi = new Vector3(-Infinity, -Infinity, -Infinity)
    const c = new Vector3()
    for (let i = 0; i < 8; i++) {
      c.set(
        i & 1 ? box.max.x : box.min.x,
        i & 2 ? box.max.y : box.min.y,
        i & 4 ? box.max.z : box.min.z,
      )
      c.applyQuaternion(q)
      lo.min(c)
      hi.max(c)
    }
    const across = (rect.width * k) / (hi.x - lo.x)
    const scale = byWidth
      ? across
      : Math.min(across, (rect.height * k) / (hi.y - lo.y))
    const centre = lo.add(hi).multiplyScalar(0.5 * scale)
    const position = toWorld(
      rect.left + rect.width / 2,
      rect.top + rect.height / 2,
    ).sub(centre)
    return {
      position,
      quaternion: q.clone(),
      scale,
      explode: 0,
      screen: 1,
      ring: 1,
      solo: 0,
    }
  }

  // ---------------------------------------------------------------- the page
  const slot = (name: string) =>
    document.querySelector<HTMLElement>(`[data-slot="${name}"]`)
  const slots = {
    hero: slot('hero'),
    waits: slot('waits'),
    knob: slot('knob'),
    server: slot('server'),
    layers: slot('layers'),
  }
  const sections = {
    waits: document.querySelector<HTMLElement>('.waits'),
    calm: document.querySelector<HTMLElement>('.calm'),
    server: document.querySelector<HTMLElement>('.server'),
    layers: document.querySelector<HTMLElement>('.layers'),
  }
  const rectOf = (el: HTMLElement | null) =>
    el?.getBoundingClientRect() ?? new DOMRect()
  const progressOf = (el: HTMLElement | null) => {
    if (!el) return 0
    const { top, height } = el.getBoundingClientRect()
    return clamp(-top / Math.max(1, height - innerHeight))
  }

  const diorama: Diorama = buildDiorama()
  scene.add(diorama.root)
  const pill = document.querySelector<HTMLElement>('[data-pill]')

  /** Where the diorama sits: the design's drawing box, fitted to its slot. */
  function placeDiorama() {
    const r = rectOf(slots.server)
    // The drawing's viewBox is 960 × 560 from (-300, -140); the plate's back
    // corner is at its origin.
    const px = r.width / 960
    const scale = (px * k) / UNIT
    diorama.root.position.copy(toWorld(r.left + 300 * px, r.top + 140 * px))
    diorama.root.quaternion.copy(qIso)
    diorama.root.scale.setScalar(scale)
    diorama.root.updateMatrixWorld(true)
    return scale
  }

  /** S5's pose for the device at section progress `p`. */
  function serverPose(p: number, still: boolean): Pose {
    const scale = placeDiorama()
    const land = still ? 1 : range(p, 0, 0.25)
    const local = diorama.mosaic
      .clone()
      .add(new Vector3(0, (1 - land) * 0.35, 0))
    const position = local
      .multiplyScalar(scale)
      .applyQuaternion(qIso)
      .add(diorama.root.position)
    const ring = still ? 1 : range(p, 0.68, 0.75)
    return {
      position,
      quaternion: qIso.clone(),
      scale,
      explode: 0,
      screen: 0,
      ring,
      solo: 0,
    }
  }

  /** Shows the server, laptop and accounts by `rise`, the pill along its path. */
  function setDiorama(rise: number, presence: number, pillAt: number | null) {
    diorama.root.visible = presence > 0.001
    diorama.root.children[0]!.scale.setScalar(Math.max(0.001, presence))
    diorama.root.children[1]!.visible = presence > 0.5
    for (const r of diorama.risers)
      r.scale.set(1, Math.max(0.001, rise * presence), 1)
    for (const a of diorama.accounts)
      a.scale.setScalar(Math.max(0.001, rise * presence))
    if (pill) {
      pill.style.opacity = pillAt === null ? '0' : String(presence)
      if (pillAt !== null) {
        const from = diorama.anchors.serverFront!
        const to = diorama.mosaic.clone().add(new Vector3(0, 84 * UNIT, 0))
        const p = from.clone().lerp(to, pillAt)
        place(pill, diorama.root.localToWorld(p))
      }
    }
  }

  // Labels that follow points in the scene ([data-anchor]) and the review
  // request pill. They move into a layer above the canvas, so the scene never
  // covers them, and keep their section's stage for measurements.
  const overlay = document.createElement('div')
  overlay.className = 'scene-labels'
  document.body.append(overlay)
  const stageOf = new Map<HTMLElement, HTMLElement>()
  const labels = [...document.querySelectorAll<HTMLElement>('[data-anchor]')]
  for (const el of [...labels, ...(pill ? [pill] : [])]) {
    stageOf.set(el, el.closest<HTMLElement>('.stage')!)
    overlay.append(el)
  }
  const project = (v: Vector3) => {
    const p = v.clone().project(camera)
    return { x: ((p.x + 1) / 2) * innerWidth, y: ((1 - p.y) / 2) * innerHeight }
  }
  /** Moves a label to a scene point. */
  function place(el: HTMLElement, world: Vector3) {
    const stage = stageOf.get(el)!.getBoundingClientRect()
    const { x, y } = project(world)
    el.style.setProperty('--ax', `${x}px`)
    el.style.setProperty('--ay', `${y}px`)
    el.style.setProperty('--sx', `${stage.left}px`)
    el.style.setProperty('--u', `${stage.width / 1440}px`)
  }

  function anchorFor(name: string, device: Device): Vector3 | null {
    if (name in diorama.anchors)
      return diorama.root.localToWorld(diorama.anchors[name]!.clone())
    if (name === 'mosaic')
      return device.root.localToWorld(
        new Vector3(
          whole.max.x,
          whole.max.y * 0.5,
          (whole.min.z + whole.max.z) / 2,
        ),
      )
    const node = device.part(name)
    if (!node) return null
    // The part's right-most point on screen, at its middle height.
    const box = new Box3().setFromObject(node)
    let best: Vector3 | null = null
    let bestX = -Infinity
    const c = new Vector3()
    for (let i = 0; i < 8; i++) {
      c.set(
        i & 1 ? box.max.x : box.min.x,
        i & 2 ? box.max.y : box.min.y,
        i & 4 ? box.max.z : box.min.z,
      )
      const x = project(c).x
      if (x > bestX) {
        bestX = x
        best = c.clone()
      }
    }
    const mid = box.getCenter(new Vector3())
    return best ? new Vector3(best.x, mid.y, best.z) : mid
  }

  function updateLabels(
    device: (section: string) => Device,
    shown: (section: string) => number,
  ) {
    scene.updateMatrixWorld(true)
    const columns = new Map<string, { el: HTMLElement; y: number }[]>()
    for (const el of labels) {
      const section = el.dataset.section!
      const world = anchorFor(el.dataset.anchor!, device(section))
      if (!world) continue
      place(el, world)
      el.style.opacity = String(shown(section))
      if (el.classList.contains('column')) {
        const list = columns.get(section) ?? []
        list.push({ el, y: parseFloat(el.style.getPropertyValue('--ay')) })
        columns.set(section, list)
      }
    }
    // Keep labels in a column from overlapping: push each below the last.
    for (const list of columns.values()) {
      list.sort((a, b) => a.y - b.y)
      let last = -Infinity
      for (const item of list) {
        const y = Math.max(item.y, last + item.el.offsetHeight + 12)
        item.el.style.setProperty('--ay', `${y}px`)
        last = y
      }
    }
  }

  // ------------------------------------------------------------ the timeline
  const devices = {
    travel: new Device(model, front),
    hero: new Device(model, front),
    waits: new Device(model, front),
    knob: new Device(model, front),
    server: new Device(model, front),
    layers: new Device(model, front),
  }
  const stills = [
    devices.hero,
    devices.waits,
    devices.knob,
    devices.server,
    devices.layers,
  ]

  const waitsStep = () => {
    const step = sections.waits?.dataset.step
    return step === '3' ? 3 : step === '2' ? 2 : 1
  }
  // Screen and ring ease towards their targets over time, like the real device.
  const shown = { screen: 1, ring: 1 }

  const poses = {
    hero: () => fit(rectOf(slots.hero), qHero, whole, true),
    waits: () => {
      const p = fit(rectOf(slots.waits), qWaits, whole, true)
      p.screen = waitsStep() === 3 ? 1 : 0
      p.ring = waitsStep() === 3 ? 0 : 1
      return p
    },
    knob: () => {
      const p = fit(rectOf(slots.knob), qKnob, dial)
      p.ring = 0
      p.solo = 1
      return p
    },
    layers: (still: boolean) => {
      const p = fit(rectOf(slots.layers), qFlat, apart)
      p.explode = still ? 1 : range(progressOf(sections.layers), 0, 0.65)
      p.screen = 0
      return p
    },
  }

  let animating = false
  let size = ''
  function frame() {
    animating = false
    if (size !== `${innerWidth}x${innerHeight}`) {
      size = `${innerWidth}x${innerHeight}`
      renderer.setSize(innerWidth, innerHeight, false)
    }
    camera.aspect = innerWidth / innerHeight
    camera.updateProjectionMatrix()
    k =
      (2 * camera.position.z * Math.tan(MathUtils.degToRad(camera.fov / 2))) /
      innerHeight

    if (motion.matches) {
      for (const d of stills) d.root.visible = false
      devices.travel.root.visible = true
      const y = scrollY
      const top = (el: HTMLElement | null) =>
        el ? el.getBoundingClientRect().top + y : 0
      const waitsStart = top(sections.waits)
      const waitsEnd =
        waitsStart + ((sections.waits?.offsetHeight ?? 0) - innerHeight) * 0.75
      const calmStart = top(sections.calm)
      const serverStart = top(sections.server)
      const serverEnd =
        serverStart + (sections.server?.offsetHeight ?? 0) - innerHeight
      const layersStart = top(sections.layers)

      const p5 = progressOf(sections.server)
      let pose: Pose
      let presence = 0
      if (y <= waitsStart) {
        pose = mixPose(
          poses.hero(),
          poses.waits(),
          smooth(y / Math.max(1, waitsStart)),
        )
      } else if (y <= waitsEnd) {
        pose = poses.waits()
      } else if (y <= calmStart) {
        const t = smooth((y - waitsEnd) / (calmStart - waitsEnd))
        pose = mixPose(poses.waits(), poses.knob(), t)
        pose.solo = range(t, 0.45, 0.95)
      } else if (y < serverStart) {
        // The knob scrolls away with its section; the device leaves with it,
        // and the diorama's plate is already waiting as S5 comes up.
        pose = poses.knob()
        presence = 1
      } else if (y <= serverEnd) {
        pose = serverPose(p5, false)
        presence = 1
      } else {
        const t = smooth((y - serverEnd) / Math.max(1, layersStart - serverEnd))
        pose = mixPose(serverPose(1, false), poses.layers(false), t)
        presence = 1 - t
      }
      if (y >= serverStart || presence) placeDiorama()
      const rise = y < serverStart ? 0 : range(p5, 0.1, 0.35)
      const pillAt =
        y >= serverStart && p5 > 0.4 && p5 < 0.72 ? range(p5, 0.42, 0.7) : null
      devices.travel.apply(pose)
      setDiorama(rise, presence, pillAt)

      // The screen and ring follow their targets smoothly.
      shown.screen += (pose.screen - shown.screen) * 0.18
      shown.ring += (pose.ring - shown.ring) * 0.12
      if (
        Math.abs(pose.screen - shown.screen) > 0.01 ||
        Math.abs(pose.ring - shown.ring) > 0.01
      )
        request()
      devices.travel.apply({ ...pose, screen: shown.screen, ring: shown.ring })

      const p6 = progressOf(sections.layers)
      updateLabels(
        () => devices.travel,
        (section) =>
          section === 'server'
            ? y >= serverStart
              ? range(p5, 0.3, 0.4) * presence
              : 0
            : y >= layersStart
              ? range(p6 * 1.55, 0.6, 0.9)
              : 0,
      )
    } else {
      devices.travel.root.visible = false
      for (const d of stills) d.root.visible = true
      devices.hero.apply(poses.hero())
      const waits = fit(rectOf(slots.waits), qWaits, whole, true)
      devices.waits.apply({ ...waits, screen: 0, ring: 1 })
      devices.knob.apply(poses.knob())
      devices.server.apply(serverPose(1, true))
      setDiorama(1, 1, 0.5)
      devices.layers.apply(poses.layers(true))
      updateLabels(
        (section) => (section === 'server' ? devices.server : devices.layers),
        () => 1,
      )
    }
    renderer.render(scene, camera)
  }

  function request() {
    if (animating) return
    animating = true
    requestAnimationFrame(frame)
  }

  addEventListener('scroll', request, { passive: true })
  addEventListener('resize', request)
  motion.addEventListener('change', request)
  for (const d of [devices.travel, ...stills]) scene.add(d.root)
  frame()
  document.documentElement.classList.add('scene-ready')
}
