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
  /** The whole device's opacity. */
  fade: number
  /** How bright the screen is: 0 off, 1 on. */
  lit: number
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
    fade: MathUtils.lerp(a.fade, b.fade, t),
    lit: MathUtils.lerp(a.lit, b.lit, t),
  }
}

/** One copy of the device: the model, its own screen and ring, its parts. */
class Device {
  readonly root = new Group()
  readonly screen = new ScreenTexture()
  private readonly ring: MeshStandardMaterial
  private screenMaterial?: MeshStandardMaterial
  private readonly parts: { node: Object3D; home: Vector3; layer: number }[] =
    []
  /** Every material, and whether it belongs to the knob (S3 keeps those). */
  private readonly materials: { m: MeshStandardMaterial; knob: boolean }[] = []
  private faded = ''

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
      const inKnob = !!part && knob.has(part.name)
      let own: MeshStandardMaterial
      if (m.name === 'diffuser') {
        // Frosted plastic over the LEDs: dark when off, glowing when lit.
        own = ring ??= m.clone()
        own.color = new Color('#2b2d30')
        own.roughness = 0.55
      } else own = m.clone()
      if (m.name === 'screen') {
        own.emissiveMap = this.screen.texture
        own.emissive = new Color(1, 1, 1)
        own.emissiveIntensity = 1
        this.screenMaterial = own
      }
      own.userData.baseOpacity = own.opacity
      own.userData.baseTransparent = own.transparent
      o.material = own
      if (!this.materials.some((x) => x.m === own))
        this.materials.push({ m: own, knob: inKnob })
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
    if (this.screenMaterial) this.screenMaterial.emissiveIntensity = pose.lit
    this.ring.emissive.copy(RING_BLUE).multiplyScalar(pose.ring * 1.6)
    const solo = Math.round(pose.solo * 50) / 50
    const fade = Math.round(pose.fade * 50) / 50
    const key = `${solo} ${fade}`
    if (key === this.faded) return
    this.faded = key
    for (const { m, knob } of this.materials) {
      const opacity = fade * (knob ? 1 : 1 - solo)
      const fading = opacity < 1
      const transparent = fading || (m.userData.baseTransparent as boolean)
      if (m.transparent !== transparent) {
        m.transparent = transparent
        m.needsUpdate = true
      }
      m.opacity = (m.userData.baseOpacity as number) * opacity
      m.depthWrite = !fading
      m.visible = opacity > 0.01
    }
  }
}

/** A cubic Bézier easing, as in CSS. */
function bezier(x1: number, y1: number, x2: number, y2: number) {
  const at = (a: number, b: number, t: number) =>
    3 * a * t * (1 - t) ** 2 + 3 * b * t * t * (1 - t) + t ** 3
  return (x: number) => {
    if (x <= 0) return 0
    if (x >= 1) return 1
    let lo = 0
    let hi = 1
    for (let i = 0; i < 24; i++) {
      const mid = (lo + hi) / 2
      if (at(x1, x2, mid) < x) lo = mid
      else hi = mid
    }
    return at(y1, y2, (lo + hi) / 2)
  }
}
/** Entrances: a strong ease-out. Movement on screen: a strong ease-in-out. */
const easeOut = bezier(0.23, 1, 0.32, 1)
const easeInOut = bezier(0.77, 0, 0.175, 1)
/** How far through a step that starts at `delay` ms and lasts `duration` ms. */
const step = (ms: number, delay: number, duration: number) =>
  clamp((ms - delay) / duration)

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

  // S3's macro has its own scene, cropped to its frame on screen: a dim key
  // light and the LED strip's warm light coming from behind the device.
  const macro = new Scene()
  macro.environment = scene.environment
  macro.environmentIntensity = 0.3
  const macroKey = new DirectionalLight(0xffffff, 0.6)
  macroKey.position.set(-0.6, 0.8, 0.9)
  macro.add(macroKey)
  const strip = new DirectionalLight(0xffa050, 1.2)
  strip.position.set(1, 0.35, -1)
  macro.add(strip)

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
  // S2 is a closer three-quarter view, so it doesn't repeat the hero's shot.
  const qWaits = turn(-24, 6)
  // S3's macro looks across the knob from the right, close to its edge.
  const qMacro = turn(-34, 10)
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
      fade: 1,
      lit: 1,
    }
  }

  // ---------------------------------------------------------------- the page
  const slot = (name: string) =>
    document.querySelector<HTMLElement>(`[data-slot="${name}"]`)
  const slots = {
    hero: slot('hero'),
    waits: slot('waits'),
    macro: slot('macro'),
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

  /**
   * S5's pose for the device, `ms` into its reveal: it fades in 12 cm above
   * the plate and settles onto it, then its ring turns blue as the review
   * request arrives. Negative `ms`: the reveal hasn't started.
   */
  function serverPose(ms: number): Pose {
    const scale = placeDiorama()
    const land = easeOut(step(ms, 0, 600))
    const local = diorama.mosaic
      .clone()
      .add(new Vector3(0, (1 - land) * 0.12, 0))
    const position = local
      .multiplyScalar(scale)
      .applyQuaternion(qIso)
      .add(diorama.root.position)
    return {
      position,
      quaternion: qIso.clone(),
      scale,
      explode: 0,
      screen: 0,
      ring: easeOut(step(ms, 1650, 500)),
      solo: 0,
      fade: ms < 0 ? 0 : easeOut(step(ms, 0, 300)),
      lit: 1,
    }
  }

  /**
   * S5's scene, `ms` into its reveal: the server, then the laptop, rise out of
   * the plate; your accounts settle in; the review request travels across.
   */
  function setDiorama(ms: number) {
    const height = 70 * UNIT
    diorama.risers.forEach((r, i) => {
      r.position.y = -(1 - easeOut(step(ms, 150 + i * 80, 500))) * height
    })
    diorama.accounts.forEach((a, i) => {
      const t = easeOut(step(ms, 300 + i * 50, 500))
      a.position.y = a.userData.homeY + (1 - t) * 20 * UNIT
      const m = (a as Mesh).material as MeshStandardMaterial
      m.transparent = true
      m.opacity = t
      a.visible = t > 0.01
    })
    if (pill) {
      const shown = Math.min(step(ms, 750, 150), 1 - step(ms, 1650, 150))
      pill.style.opacity = String(shown)
      if (shown > 0) {
        const from = diorama.anchors.serverFront!
        const to = diorama.mosaic.clone().add(new Vector3(0, 84 * UNIT, 0))
        const p = from.clone().lerp(to, easeInOut(step(ms, 750, 900)))
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
    shown: (section: string, index: number) => number,
  ) {
    scene.updateMatrixWorld(true)
    const columns = new Map<string, { el: HTMLElement; y: number }[]>()
    const count = new Map<string, number>()
    for (const el of labels) {
      const section = el.dataset.section!
      const index = count.get(section) ?? 0
      count.set(section, index + 1)
      const world = anchorFor(el.dataset.anchor!, device(section))
      if (!world) continue
      place(el, world)
      el.style.opacity = String(shown(section, index))
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
    macro: new Device(model, front),
    server: new Device(model, front),
    layers: new Device(model, front),
  }
  for (const a of diorama.accounts) {
    a.userData.homeY = a.position.y
    const mesh = a as Mesh
    mesh.material = (mesh.material as MeshStandardMaterial).clone()
  }

  // The two reveals play once, on a timer, from when their slot is in view.
  const revealed: { waits?: number; server?: number } = {}
  const LONGEST = 2400
  function reveal(
    name: 'waits' | 'server',
    el: HTMLElement | null,
    share: number,
  ) {
    if (!el) return
    const observer = new IntersectionObserver(
      ([entry]) => {
        if (!entry?.isIntersecting) return
        observer.disconnect()
        revealed[name] ??= performance.now()
        if (name === 'waits') sections.waits?.setAttribute('data-arrived', '')
        request()
      },
      { threshold: share },
    )
    observer.observe(el)
  }
  reveal('waits', slots.waits, 0.4)
  reveal('server', slots.server, 0.3)
  const since = (name: 'waits' | 'server') =>
    revealed[name] === undefined ? -1 : performance.now() - revealed[name]!

  const poses = {
    hero: () => fit(rectOf(slots.hero), qHero, whole, true),
    waits: (ms: number) => {
      const p = fit(rectOf(slots.waits), qWaits, whole, true)
      // It arrives: the ring turns blue, then the review request fades in
      // over the calendar.
      p.ring = easeOut(step(ms, 150, 600))
      p.screen = 1 - easeOut(step(ms, 250, 400))
      return p
    },
    macro: () => {
      // The knob a fifth of the frame wide, right of centre: the dark screen
      // runs off to the left and the device's edge, with the glow behind
      // it, shows on the right.
      const r = rectOf(slots.macro)
      const size = dial.getSize(new Vector3())
      const scale = (r.width * 0.21 * k) / Math.max(size.x, size.y)
      const centre = dial
        .getCenter(new Vector3())
        .applyQuaternion(qMacro)
        .multiplyScalar(scale)
      const position = toWorld(
        r.left + r.width * 0.58,
        r.top + r.height * 0.5,
      ).sub(centre)
      const p = fit(r, qMacro, whole)
      p.position = position
      p.scale = scale
      p.ring = 0
      p.screen = 1
      p.lit = 0
      return p
    },
    layers: () => {
      const p = fit(rectOf(slots.layers), qFlat, apart)
      p.explode = 1
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

    // Each section's still: the hero, S2 and S3 are always their own copy.
    devices.hero.apply(poses.hero())
    devices.macro.apply(poses.macro())
    placeDiorama()

    if (motion.matches) {
      devices.server.root.visible = false
      devices.layers.root.visible = false
      devices.travel.root.visible = true
      const y = scrollY
      const top = (el: HTMLElement | null) =>
        el ? el.getBoundingClientRect().top + y : 0
      devices.waits.apply(poses.waits(since('waits')))

      // The one scroll-driven moment: from just after S5 reaches the top of
      // the screen until S6 does, the device lifts off the plate, travels and
      // comes apart on the way.
      const moveStart = top(sections.server) + innerHeight * 0.15
      const moveEnd = Math.max(moveStart + 1, top(sections.layers))
      const move = easeInOut(clamp((y - moveStart) / (moveEnd - moveStart)))
      // Scrolling into the move before S5 played finishes its reveal.
      if (move > 0 && revealed.server === undefined)
        revealed.server = performance.now() - LONGEST
      const ms5 = since('server')

      let pose: Pose
      if (move <= 0) pose = serverPose(ms5)
      else if (move >= 1) pose = poses.layers()
      else {
        pose = mixPose(serverPose(ms5), poses.layers(), move)
        pose.explode = clamp((move - 0.1) / 0.75)
      }
      devices.travel.apply(pose)
      setDiorama(ms5)

      updateLabels(
        () => devices.travel,
        (section, i) =>
          section === 'server'
            ? easeOut(step(ms5, 600 + i * 60, 220)) * (1 - clamp(move / 0.2))
            : easeOut(clamp((move - 0.85 - i * 0.03) / 0.1)),
      )
      if (
        (revealed.waits !== undefined && since('waits') < LONGEST) ||
        (revealed.server !== undefined && ms5 < LONGEST)
      )
        request()
    } else {
      devices.travel.root.visible = false
      devices.server.root.visible = true
      devices.layers.root.visible = true
      devices.waits.apply(poses.waits(Infinity))
      devices.server.apply(serverPose(Infinity))
      setDiorama(Infinity)
      devices.layers.apply(poses.layers())
      updateLabels(
        (section) => (section === 'server' ? devices.server : devices.layers),
        () => 1,
      )
    }
    renderer.render(scene, camera)

    // S3's macro, cropped to its frame.
    const r = rectOf(slots.macro)
    if (r.bottom > 0 && r.top < innerHeight && r.width > 0) {
      renderer.autoClear = false
      renderer.clearDepth()
      renderer.setScissorTest(true)
      renderer.setScissor(r.left, innerHeight - r.bottom, r.width, r.height)
      renderer.render(macro, camera)
      renderer.setScissorTest(false)
      renderer.autoClear = true
    }
  }

  function request() {
    if (animating) return
    animating = true
    requestAnimationFrame(frame)
  }

  addEventListener('scroll', request, { passive: true })
  addEventListener('resize', request)
  motion.addEventListener('change', request)
  for (const [name, d] of Object.entries(devices))
    (name === 'macro' ? macro : scene).add(d.root)
  frame()
  document.documentElement.classList.add('scene-ready')
}
