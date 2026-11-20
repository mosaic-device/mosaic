// Plays the S5–S6 shot from rendered frames. The reveal (frames a-*) plays
// once on a timer when the stage comes into view; scroll then drives frames
// b-*: the move (0..bMove) and the final pull, which ends as S6's copy and
// labels settle. Labels follow per-frame anchors from the renderer.

interface Data {
  size: [number, number]
  a: number
  b: number
  bMove: number
  anchors: {
    a: Record<string, [number, number]>[]
    b: Record<string, [number, number]>[]
  }
  frames: { a: string[]; ring: string; b: string[] }
}

const clamp = (v: number, lo = 0, hi = 1) => Math.min(hi, Math.max(lo, v))
const smooth = (v: number) => {
  const t = clamp(v)
  return t * t * (3 - 2 * t)
}
/** cubic-bezier(0.23, 1, 0.32, 1), for things arriving. */
const easeOut = (t: number) => 1 - (1 - clamp(t)) ** 4
const easeInOut = (t: number) => {
  const x = clamp(t)
  return x < 0.5 ? 8 * x ** 4 : 1 - (-2 * x + 2) ** 4 / 2
}
const step = (ms: number, delay: number, duration: number) =>
  clamp((ms - delay) / duration)

// Where the scroll stretch goes: a short pause on S5, the move, the final
// pull (ending with S6's copy and labels), then a hold.
const MOVE_START = 0.06
const MOVE_END = 0.55
const SETTLE_END = 0.8
const A_FPS = 24
const RING_AT = 1650

export function playJourney(section: HTMLElement) {
  const data = JSON.parse(
    section.querySelector('#journey-data')!.textContent!,
  ) as Data
  const stage = section.querySelector<HTMLElement>('.pin .stage')!
  const canvas = stage.querySelector('canvas')!
  const ctx = canvas.getContext('2d')!
  const serverCopy = stage.querySelector<HTMLElement>('.server-copy')!
  const layersCopy = stage.querySelector<HTMLElement>('.layers-copy')!
  const pill = stage.querySelector<HTMLElement>('.pill')!
  const labels = [...stage.querySelectorAll<HTMLElement>('[data-anchor]')]

  const load = (src: string) => {
    const img = new Image()
    img.decoding = 'async'
    img.src = src
    return img
  }
  let images: {
    a: HTMLImageElement[]
    ring: HTMLImageElement
    b: HTMLImageElement[]
  } | null = null
  let revealedAt: number | null = null

  function loadAll() {
    if (images) return
    images = {
      a: data.frames.a.map(load),
      ring: load(data.frames.ring),
      b: data.frames.b.map(load),
    }
    Promise.all(
      [...images.a, images.ring, ...images.b.slice(0, 1)].map((i) =>
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
      // The reveal, at 24 frames a second, then the ring turning blue.
      const i = Math.max(
        0,
        Math.min(data.a - 1, Math.floor((ms * A_FPS) / 1000)),
      )
      anchors = data.anchors.a[i]!
      if (images) {
        draw(images.a[i]!)
        const ring = easeOut(step(ms, RING_AT, 500))
        if (ring > 0) draw(images.ring, ring)
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

    // The review request travels from your server to Mosaic.
    const shown =
      Math.min(step(ms, 750, 150), 1 - step(ms, 1650, 150)) * (1 - leaving)
    pill.style.opacity = String(shown)
    if (shown > 0) {
      const last = data.anchors.a[data.a - 1]!
      const from = point(last, 'serverFront')
      const to = point(last, 'mosaicTop')
      if (from && to) {
        const t = easeInOut(step(ms, 750, 900))
        pill.style.setProperty('--ax', `${from.x + (to.x - from.x) * t}px`)
        pill.style.setProperty('--ay', `${from.y + (to.y - from.y) * t}px`)
      }
    }

    // Labels: S5's arrive after the landing; S6's one after another, the last
    // landing as the pull ends.
    const columns: { el: HTMLElement; y: number }[] = []
    const index = { server: 0, layers: 0 }
    for (const el of labels) {
      const group = el.dataset.group as 'server' | 'layers'
      const i = index[group]++
      const p = point(anchors, el.dataset.anchor!)
      const opacity =
        group === 'server'
          ? easeOut(step(ms, 600 + i * 60, 220)) * (1 - leaving)
          : easeOut(clamp((settled - 0.35 - i * 0.1) / 0.25))
      el.style.opacity = String(opacity)
      el.style.visibility = opacity > 0 ? '' : 'hidden'
      if (!p) continue
      el.style.setProperty('--ax', `${p.x}px`)
      el.style.setProperty('--ay', `${p.y}px`)
      if (el.classList.contains('column') && group === 'layers')
        columns.push({ el, y: p.y })
    }
    // Keep S6's column of labels from overlapping.
    columns.sort((a, b) => a.y - b.y)
    let last = -Infinity
    for (const c of columns) {
      const y = Math.max(c.y, last + c.el.offsetHeight + 12)
      c.el.style.setProperty('--ay', `${y}px`)
      last = y
    }

    // Keep playing while the reveal runs.
    if (revealedAt !== null && ms < RING_AT + 600) request()
  }

  addEventListener('scroll', request, { passive: true })
  addEventListener('resize', () => {
    size()
    request()
  })
  size()
  request()
}
