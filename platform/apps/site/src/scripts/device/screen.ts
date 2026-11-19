// Draws the device's notification screens into a canvas for the 3D screen.
// Mirrors DeviceScreen.astro, on a 640 × 360 layout drawn at `scale`.
import { CanvasTexture, SRGBColorSpace } from 'three'

export type Variant = 'review' | 'calendar'

const C = {
  bg: '#090A0A',
  fg: '#F2F2EF',
  muted: '#92979B',
  low: '#666B70',
  raised: '#222426',
  surface: '#141516',
  border: '#303335',
  primary: '#7540B5',
  add: '#4ADE80',
  del: '#F87171',
}
const SANS = 'Manrope, ui-sans-serif, system-ui, sans-serif'
const BRAND = 'Outfit, ui-sans-serif, system-ui, sans-serif'

function roundRect(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  w: number,
  h: number,
  r: number,
) {
  ctx.beginPath()
  ctx.roundRect(x, y, w, h, r)
}

function buttons(
  ctx: CanvasRenderingContext2D,
  labels: [string, string, string],
) {
  ctx.font = `500 15px ${SANS}`
  ctx.textBaseline = 'middle'
  let x = 26
  const y = 360 - 22 - 44
  labels.forEach((label, i) => {
    const pad = i === 0 ? 22 : i === 1 ? 20 : 14
    const w = ctx.measureText(label).width + pad * 2
    if (i === 0) {
      ctx.fillStyle = C.primary
      roundRect(ctx, x, y, w, 44, 8)
      ctx.fill()
      ctx.fillStyle = '#FFFFFF'
    } else if (i === 1) {
      ctx.fillStyle = C.surface
      roundRect(ctx, x + 0.5, y + 0.5, w - 1, 43, 8)
      ctx.fill()
      ctx.strokeStyle = C.border
      ctx.stroke()
      ctx.fillStyle = C.fg
    } else {
      ctx.fillStyle = C.muted
    }
    ctx.fillText(label, x + pad, y + 22)
    x += w + 10
  })
}

function source(ctx: CanvasRenderingContext2D, name: string, detail: string) {
  const y = 70
  ctx.fillStyle = C.raised
  roundRect(ctx, 26, y, 28, 28, 8)
  ctx.fill()
  ctx.strokeStyle = C.fg
  ctx.lineWidth = 1.5
  ctx.strokeRect(26 + 8, y + 8, 12, 12)
  ctx.lineWidth = 1
  ctx.textBaseline = 'middle'
  ctx.font = `500 13px ${SANS}`
  ctx.fillStyle = C.fg
  ctx.fillText(name, 64, y + 14)
  const w = ctx.measureText(name).width
  ctx.font = `400 13px ${SANS}`
  ctx.fillStyle = C.muted
  ctx.fillText(detail, 64 + w + 10, y + 14)
}

function drawVariant(ctx: CanvasRenderingContext2D, variant: Variant) {
  ctx.textBaseline = 'alphabetic'
  if (variant === 'review') {
    source(ctx, 'GitHub', 'mosaic-device/mosaic')
    ctx.textBaseline = 'top'
    ctx.fillStyle = C.fg
    ctx.font = `500 30px ${SANS}`
    ctx.fillText('Avery requested your review', 26, 114)
    ctx.fillStyle = C.muted
    ctx.font = `400 16px ${SANS}`
    ctx.fillText(
      'Show and run notification actions on the device · #194',
      26,
      160,
    )
    ctx.font = `400 13px ${SANS}`
    let x = 26
    for (const [text, color] of [
      ['+412', C.add],
      ['−88', C.del],
      ['12 files', C.muted],
      ['2 min ago', C.muted],
    ] as const) {
      ctx.fillStyle = color
      ctx.fillText(text, x, 194)
      x += ctx.measureText(text).width + 12
    }
    buttons(ctx, ['Approve', 'Open on laptop', 'Dismiss'])
  } else {
    source(ctx, 'Google Calendar', '10:00 – 10:15')
    ctx.textBaseline = 'top'
    ctx.fillStyle = C.fg
    ctx.font = `500 30px ${SANS}`
    ctx.fillText('Firmware standup', 26, 114)
    ctx.font = `500 64px ${SANS}`
    ctx.fillText('in 4 min', 24, 158)
    buttons(ctx, ['Join on laptop', 'Snooze 5 min', 'Dismiss'])
  }
}

/** A screen texture that can fade between the two notifications. */
export class ScreenTexture {
  readonly texture: CanvasTexture
  private readonly ctx: CanvasRenderingContext2D
  private mix = -1

  constructor(private readonly scale = 2) {
    const canvas = document.createElement('canvas')
    canvas.width = 640 * scale
    canvas.height = 360 * scale
    this.ctx = canvas.getContext('2d')!
    this.texture = new CanvasTexture(canvas)
    this.texture.colorSpace = SRGBColorSpace
    this.texture.flipY = false
    this.texture.anisotropy = 4
  }

  /** 0 shows the review request, 1 the calendar, between them a crossfade. */
  set(mix: number) {
    const m = Math.round(mix * 40) / 40
    if (m === this.mix) return
    this.mix = m
    const { ctx, scale } = this
    ctx.setTransform(scale, 0, 0, scale, 0, 0)
    ctx.globalAlpha = 1
    ctx.fillStyle = C.bg
    ctx.fillRect(0, 0, 640, 360)

    // Status bar.
    ctx.textBaseline = 'middle'
    ctx.font = `500 13px ${BRAND}`
    ctx.fillStyle = C.fg
    ;(
      ctx as CanvasRenderingContext2D & { letterSpacing: string }
    ).letterSpacing = '4.4px'
    ctx.fillText('MOSAIC', 26, 30)
    ;(
      ctx as CanvasRenderingContext2D & { letterSpacing: string }
    ).letterSpacing = '0px'
    ctx.font = `400 13px ${SANS}`
    ctx.fillStyle = C.muted
    ctx.textAlign = 'right'
    ctx.fillText('9:50', 614, 30)
    ctx.textAlign = 'left'
    ctx.fillStyle = C.fg
    roundRect(ctx, 556, 28, 14, 4, 2)
    ctx.fill()
    ctx.fillStyle = C.low
    roundRect(ctx, 574, 28, 4, 4, 2)
    ctx.fill()
    roundRect(ctx, 582, 28, 4, 4, 2)
    ctx.fill()

    if (m < 1) {
      ctx.globalAlpha = 1 - m
      drawVariant(ctx, 'review')
    }
    if (m > 0) {
      ctx.globalAlpha = m
      drawVariant(ctx, 'calendar')
    }
    ctx.globalAlpha = 1
    this.texture.needsUpdate = true
  }
}
