// Plays the S5–S6 shot from rendered frames. The reveal (frames a-*) plays
// once on a timer when the stage comes into view, then a notification loops:
// light runs inside the pipes from your accounts, through your server, into
// Mosaic, its ring glows, and the light runs on to your laptop. Scroll then
// drives frames b-*: the move (0..bMove) and the final pull, which ends as
// S6's copy and labels settle. Labels and pipes follow points from the
// renderer.

interface Data {
  size: [number, number]
  a: number
  b: number
  bMove: number
  anchors: {
    a: Record<string, [number, number]>[]
    b: Record<string, [number, number]>[]
  }
  /** Each pipe's route through its middle, as frame points. */
  pipes: Record<string, [number, number][]>
  frames: { a: string[]; ring: string; pipes: string; b: string[] }
}

const clamp = (v: number, lo = 0, hi = 1) => Math.min(hi, Math.max(lo, v))
const smooth = (v: number) => {
  const t = clamp(v)
  return t * t * (3 - 2 * t)
}
/** cubic-bezier(0.23, 1, 0.32, 1), for things arriving. */
const easeOut = (t: number) => 1 - (1 - clamp(t)) ** 4
const step = (ms: number, delay: number, duration: number) =>
  clamp((ms - delay) / duration)

// Where the scroll stretch goes: a short pause on S5, the move, the final
// pull (ending with S6's copy and labels), then a hold.
const MOVE_START = 0.06
const MOVE_END = 0.55
const SETTLE_END = 0.8
const A_FPS = 24
// The notification loop, in ms: it starts once the reveal has landed. The
// light runs from your accounts into your server and on into Mosaic, the
// ring glows, the light runs on to your laptop, the ring fades, then a rest.
const LOOP_START = 1200
const LOOP = 5400
const ACCOUNTS_RUN = [0, 1300] as const
const MOSAIC_RUN = [1150, 800] as const
const RING_UP = [1850, 400] as const
const LAPTOP_RUN = [2250, 1000] as const
const RING_DOWN = [3300, 700] as const
// The light's colour, the ring's blue.
const BLUE = '40 110 255'

export function playJourney(section: HTMLElement) {
  const data = JSON.parse(
    section.querySelector('#journey-data')!.textContent!,
  ) as Data
  const stage = section.querySelector<HTMLElement>('.pin .stage')!
  const canvas = stage.querySelector('canvas')!
  const ctx = canvas.getContext('2d')!
  const serverCopy = stage.querySelector<HTMLElement>('.server-copy')!
  const layersCopy = stage.querySelector<HTMLElement>('.layers-copy')!
  const labels = [...stage.querySelectorAll<HTMLElement>('[data-anchor]')]
  // S6's labels arrive one after another, the last landing as the pull ends.
  const layerCount = labels.filter((l) => l.dataset.group === 'layers').length
  const layerStagger = 0.4 / Math.max(1, layerCount - 1)

  const load = (src: string) => {
    const img = new Image()
    img.decoding = 'async'
    img.src = src
    return img
  }
  let images: {
    a: HTMLImageElement[]
    ring: HTMLImageElement
    pipes: HTMLImageElement
    b: HTMLImageElement[]
  } | null = null
  let revealedAt: number | null = null

  function loadAll() {
    if (images) return
    images = {
      a: data.frames.a.map(load),
      ring: load(data.frames.ring),
      pipes: load(data.frames.pipes),
      b: data.frames.b.map(load),
    }
    Promise.all(
      [...images.a, images.ring, images.pipes, ...images.b.slice(0, 1)].map((i) =>
        i.decode(),
      ),
    )
      .then(() => {
        section.classList.add('loaded')
        request()
      })
      .catch(() => {
        // Leave the poster.
      })
  }

  // Start loading well before the shot, and play the reveal once it is in view.
  new IntersectionObserver(
    ([e]) => {
      if (e?.isIntersecting) loadAll()
    },
    { rootMargin: '150% 0px' },
  ).observe(section)
  const reveal = new IntersectionObserver(
    ([e]) => {
      if (!e?.isIntersecting) return
      reveal.disconnect()
      revealedAt ??= performance.now()
      request()
    },
    { threshold: 0.3 },
  )
  reveal.observe(stage)
  // The loop only runs while the stage is on screen.
  let inView = false
  new IntersectionObserver(([e]) => {
    inView = !!e?.isIntersecting
    if (inView) request()
  }).observe(stage)

  let fit = { scale: 1, x: 0, y: 0 }
  function size() {
    const { width, height } = stage.getBoundingClientRect()
    const dpr = Math.min(devicePixelRatio, 2)
    canvas.width = Math.round(width * dpr)
    canvas.height = Math.round(height * dpr)
    // Cover the stage, centred.
    const scale = Math.max(width / data.size[0], height / data.size[1])
    fit = {
      scale,
      x: (width - data.size[0] * scale) / 2,
      y: (height - data.size[1] * scale) / 2,
    }
  }

  function draw(img: HTMLImageElement, alpha = 1) {
    if (!img.complete || !img.naturalWidth) return
    const dpr = canvas.width / stage.getBoundingClientRect().width
    ctx.globalAlpha = alpha
    ctx.drawImage(
      img,
      fit.x * dpr,
      fit.y * dpr,
      data.size[0] * fit.scale * dpr,
      data.size[1] * fit.scale * dpr,
    )
    ctx.globalAlpha = 1
  }

  /** A frame's anchor, in stage pixels. */
  const point = (anchors: Record<string, [number, number]>, name: string) => {
    const p = anchors[name]
    if (!p) return null
    return {
      x: fit.x + p[0] * data.size[0] * fit.scale,
      y: fit.y + p[1] * data.size[1] * fit.scale,
    }
  }

  // The light is drawn on its own layer, then cut to the pipes' shape from
  // the mask render (white where a pipe shows), so it sits inside them.
  const light = document.createElement('canvas')
  const lightCtx = light.getContext('2d')!
  let mask: HTMLCanvasElement | null = null
  function pipeMask() {
    if (mask || !images?.pipes.naturalWidth) return mask
    mask = document.createElement('canvas')
    mask.width = images.pipes.naturalWidth
    mask.height = images.pipes.naturalHeight
    const m = mask.getContext('2d')!
    m.drawImage(images.pipes, 0, 0)
    const px = m.getImageData(0, 0, mask.width, mask.height)
    for (let i = 0; i < px.data.length; i += 4) {
      const a = (px.data[i]! * px.data[i + 3]!) / 255
      px.data[i] = px.data[i + 1] = px.data[i + 2] = 255
      px.data[i + 3] = a
    }
    m.putImageData(px, 0, 0)
    return mask
  }

  /**
   * Light running inside a pipe: `head` goes 0 to 1 as it enters, runs the
   * length and drains out the far end.
   */
  function flow(path: [number, number][], head: number) {
    if (head <= 0 || head >= 1) return
    const dpr = canvas.width / stage.getBoundingClientRect().width
    const pts = path.map(([x, y]) => ({
      x: (fit.x + x * data.size[0] * fit.scale) * dpr,
      y: (fit.y + y * data.size[1] * fit.scale) * dpr,
    }))
    const at = [0]
    for (let i = 1; i < pts.length; i++)
      at.push(at[i - 1]! + Math.hypot(pts[i]!.x - pts[i - 1]!.x, pts[i]!.y - pts[i - 1]!.y))
    const trail = 90 * fit.scale * dpr
    const front = head * (at[at.length - 1]! + trail)
    lightCtx.lineCap = 'round'
    lightCtx.lineWidth = 16 * fit.scale * dpr
    for (let i = 1; i < pts.length; i++) {
      if (at[i]! > front || at[i]! < front - trail) continue
      const t = (at[i]! - (front - trail)) / trail
      lightCtx.strokeStyle = `rgb(${BLUE} / ${t * t})`
      lightCtx.beginPath()
      lightCtx.moveTo(pts[i - 1]!.x, pts[i - 1]!.y)
      lightCtx.lineTo(pts[i]!.x, pts[i]!.y)
      lightCtx.stroke()
    }
  }

  /** Composites the pipes' light, cut to the pipes, with a soft bloom. */
  function shine(alpha: number) {
    const m = pipeMask()
    if (!m || alpha <= 0 || !light.width || !light.height) return
    const dpr = canvas.width / stage.getBoundingClientRect().width
    lightCtx.globalCompositeOperation = 'destination-in'
    lightCtx.drawImage(
      m,
      fit.x * dpr,
      fit.y * dpr,
      data.size[0] * fit.scale * dpr,
      data.size[1] * fit.scale * dpr,
    )
    lightCtx.globalCompositeOperation = 'source-over'
    ctx.save()
    ctx.globalCompositeOperation = 'lighter'
    ctx.globalAlpha = 0.7 * alpha
    ctx.filter = `blur(${8 * dpr}px)`
    ctx.drawImage(light, 0, 0)
    ctx.filter = 'none'
    ctx.globalAlpha = alpha
    ctx.drawImage(light, 0, 0)
    ctx.restore()
  }

  let queued = false
  function request() {
    if (queued) return
    queued = true
    requestAnimationFrame(frame)
  }

  function frame() {
    queued = false
    const { top, height } = section.getBoundingClientRect()
    const q = clamp(-top / Math.max(1, height - innerHeight))
    // Scrolling into the move before the reveal played finishes it.
    if (q > MOVE_START && revealedAt === null)
      revealedAt = performance.now() - 5000
    const ms = revealedAt === null ? -1 : performance.now() - revealedAt

    ctx.clearRect(0, 0, canvas.width, canvas.height)
    let anchors: Record<string, [number, number]>
    if (q <= MOVE_START) {
      // The reveal, at 24 frames a second, then the notification loop. As
      // the move nears, the loop gives way and the ring settles lit, as the
      // move's frames have it.
      const i = Math.max(
        0,
        Math.min(data.a - 1, Math.floor((ms * A_FPS) / 1000)),
      )
      anchors = data.anchors.a[i]!
      const nearing = smooth(q / MOVE_START)
      const t = ms >= LOOP_START ? (ms - LOOP_START) % LOOP : -1
      const pulse =
        t < 0
          ? 0
          : easeOut(step(t, ...RING_UP)) * (1 - smooth(step(t, ...RING_DOWN)))
      if (images) {
        draw(images.a[i]!)
        const ring = Math.max(pulse, nearing)
        if (ring > 0) draw(images.ring, ring)
      }
      if (t >= 0 && nearing < 1) {
        if (light.width !== canvas.width || light.height !== canvas.height) {
          light.width = canvas.width
          light.height = canvas.height
        } else lightCtx.clearRect(0, 0, light.width, light.height)
        flow(data.pipes.accounts!, step(t, ...ACCOUNTS_RUN))
        flow(data.pipes.mosaic!, step(t, ...MOSAIC_RUN))
        flow(data.pipes.laptop!, step(t, ...LAPTOP_RUN))
        shine(1 - nearing)
      }
    } else {
      const move = clamp((q - MOVE_START) / (MOVE_END - MOVE_START))
      const settle = clamp((q - MOVE_END) / (SETTLE_END - MOVE_END))
      const i =
        q < MOVE_END
          ? Math.round(move * (data.bMove - 1))
          : data.bMove + Math.round(settle * (data.b - 1 - data.bMove))
      anchors = data.anchors.b[i]!
      if (images) draw(images.b[i]!)
    }

    // Copy: S5's leaves as the push-in starts; S6's settles with the pull.
    const leaving = smooth((q - 0.08) / 0.12)
    const settled = clamp((q - MOVE_END) / (SETTLE_END - MOVE_END))
    serverCopy.style.opacity = String(1 - leaving)
    serverCopy.style.visibility = leaving >= 1 ? 'hidden' : ''
    layersCopy.style.opacity = String(smooth(settled))

    // Labels: S5's arrive after the landing; S6's one after another, the last
    // landing as the pull ends.
    const columns: { el: HTMLElement; x: number; y: number }[] = []
    const index = { server: 0, layers: 0 }
    for (const el of labels) {
      const group = el.dataset.group as 'server' | 'layers'
      const i = index[group]++
      const p = point(anchors, el.dataset.anchor!)
      const opacity =
        group === 'server'
          ? easeOut(step(ms, 600 + i * 60, 220)) * (1 - leaving)
          : easeOut(clamp((settled - 0.35 - i * layerStagger) / 0.25))
      el.style.opacity = String(opacity)
      el.style.visibility = opacity > 0 ? '' : 'hidden'
      if (!p) continue
      el.style.setProperty('--ax', `${p.x}px`)
      el.style.setProperty('--ay', `${p.y}px`)
      if (el.classList.contains('column') && group === 'layers')
        columns.push({ el, x: p.x, y: p.y })
    }
    // Keep S6's column of labels from overlapping, each leader running from
    // its label back to its point.
    columns.sort((a, b) => a.y - b.y)
    let last = -Infinity
    for (const c of columns) {
      const y = Math.max(c.y, last + c.el.offsetHeight + 12)
      c.el.style.setProperty('--ly', `${y}px`)
      last = y
      const fromX = c.el.offsetLeft - 8
      const dx = c.x - fromX
      const dy = c.y - y
      c.el.style.setProperty('--len', `${Math.max(0, Math.hypot(dx, dy) - 8)}px`)
      c.el.style.setProperty('--angle', `${Math.atan2(dy, dx) - Math.PI}rad`)
    }

    // Keep playing through the reveal, and loop while S5 is on screen.
    if (revealedAt !== null && inView && q <= MOVE_START) request()
  }

  addEventListener('scroll', request, { passive: true })
  addEventListener('resize', () => {
    size()
    request()
  })
  size()
  request()
}
