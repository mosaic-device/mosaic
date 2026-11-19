// The device model pulled apart into its layers. The model lies flat, screen up,
// and each part lifts by its layer as `progress` goes from 0 to 1. Labels in the
// page follow their parts with a leader line. Renders only when something changes.
import {
  ACESFilmicToneMapping,
  Box3,
  Color,
  DirectionalLight,
  Group,
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

// How high each part lifts, in layers, front of the device on top.
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
const LAYER_GAP = 0.032 // metres between layers when fully apart

export interface Exploded {
  setProgress(progress: number): void
  resize(): void
}

export async function mountExploded(
  canvas: HTMLCanvasElement,
  labels: { part: string; label: HTMLElement; leader: SVGLineElement }[],
  stage: HTMLElement,
): Promise<Exploded> {
  const renderer = new WebGLRenderer({ canvas, antialias: true, alpha: true })
  renderer.setPixelRatio(Math.min(devicePixelRatio, 2))
  renderer.outputColorSpace = SRGBColorSpace
  renderer.toneMapping = ACESFilmicToneMapping
  renderer.toneMappingExposure = 1.1

  const scene = new Scene()
  const pmrem = new PMREMGenerator(renderer)
  scene.environment = pmrem.fromScene(new RoomEnvironment(), 0.04).texture
  scene.environmentIntensity = 0.55
  const key = new DirectionalLight(0xffffff, 1.4)
  key.position.set(-0.4, 1, 0.6)
  scene.add(key)
  const warm = new DirectionalLight(0xffa050, 0.8)
  warm.position.set(0.3, -1, 0.4)
  scene.add(warm)

  const gltf = await new GLTFLoader()
    .setMeshoptDecoder(MeshoptDecoder)
    .loadAsync(modelUrl)
  const model = gltf.scene

  // The page's device is dark, as in the design; the generator's case colour is
  // only a placeholder.
  model.traverse((o) => {
    if (!(o instanceof Mesh)) return
    const m = o.material as MeshStandardMaterial
    if (m.name === 'case') {
      m.color = new Color('#2a2c2e')
      m.roughness = 0.7
    }
  })

  // Lay it flat: turn the screen's normal to point straight up.
  let front = new Vector3(0, 0, 1)
  model.traverse((o) => {
    if (
      o instanceof Mesh &&
      (o.material as MeshStandardMaterial).name === 'screen'
    ) {
      const n = o.geometry.getAttribute('normal')
      front = new Vector3(n.getX(0), n.getY(0), n.getZ(0))
        .applyQuaternion(o.getWorldQuaternion(new Quaternion()))
        .normalize()
    }
  })
  const flat = new Group()
  flat.quaternion.setFromUnitVectors(front, new Vector3(0, 1, 0))
  flat.add(model)
  scene.add(flat)

  const parts = new Map<string, { node: Object3D; home: Vector3 }>()
  for (const node of model.children)
    parts.set(node.name, { node, home: node.position.clone() })
  // Lifting along world up, expressed in the model's own frame.
  const up = new Vector3(0, 1, 0).applyQuaternion(
    flat.quaternion.clone().invert(),
  )

  const camera = new PerspectiveCamera(26, 1, 0.01, 10)
  let progress = 0

  function place(p: number) {
    const eased = p * p * (3 - 2 * p)
    for (const [name, { node, home }] of parts)
      node.position
        .copy(home)
        .addScaledVector(up, (LAYERS[name] ?? 0) * LAYER_GAP * eased)
  }

  // Frame the fully apart model once, so the camera holds still while it opens.
  place(1)
  flat.updateMatrixWorld(true)
  const box = new Box3().setFromObject(flat)
  const centre = box.getCenter(new Vector3())
  const size = box.getSize(new Vector3()).length()
  const direction = new Vector3(0.62, 0.62, 0.9).normalize()

  const corner = new Vector3()
  function render() {
    place(progress)
    renderer.render(scene, camera)
    const rect = canvas.getBoundingClientRect()
    const stageRect = stage.getBoundingClientRect()
    const shown = Math.min(1, Math.max(0, (progress - 0.55) / 0.35))
    for (const { part, label, leader } of labels) {
      const entry = parts.get(part)
      if (!entry) continue
      // The part's right-most point on screen, at its middle height.
      const b = new Box3().setFromObject(entry.node)
      let right = -Infinity
      let y = 0
      for (let i = 0; i < 8; i++) {
        corner
          .set(
            i & 1 ? b.max.x : b.min.x,
            i & 2 ? b.max.y : b.min.y,
            i & 4 ? b.max.z : b.min.z,
          )
          .project(camera)
        right = Math.max(right, corner.x)
      }
      b.getCenter(corner).project(camera)
      y = corner.y
      const fromX =
        rect.left - stageRect.left + ((right + 1) / 2) * rect.width + 12
      const atY = rect.top - stageRect.top + ((1 - y) / 2) * rect.height
      const labelBox = label.getBoundingClientRect()
      const toX = labelBox.left - stageRect.left - 12
      label.style.top = `${atY}px`
      label.style.opacity = String(shown)
      leader.setAttribute('x1', String(fromX))
      leader.setAttribute('x2', String(toX))
      leader.setAttribute('y1', String(atY))
      leader.setAttribute('y2', String(atY))
      leader.style.opacity = String(shown)
    }
  }

  function resize() {
    const { width, height } = canvas.getBoundingClientRect()
    if (!width || !height) return
    renderer.setSize(width, height, false)
    camera.aspect = width / height
    const fit = size / 2 / Math.tan((camera.fov * Math.PI) / 360)
    camera.position.copy(centre).addScaledVector(direction, fit * 1.05)
    camera.lookAt(centre)
    camera.updateProjectionMatrix()
    render()
  }

  resize()
  return {
    setProgress(p: number) {
      const next = Math.min(1, Math.max(0, p))
      if (next === progress) return
      progress = next
      render()
    },
    resize,
  }
}
