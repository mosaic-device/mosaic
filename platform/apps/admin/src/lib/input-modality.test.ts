import { afterEach, describe, expect, it } from 'vitest'
import { trackInputModality } from '@/lib/input-modality'

describe('trackInputModality', () => {
  let stop = () => {}
  afterEach(() => {
    stop()
    delete document.documentElement.dataset.input
  })

  it('marks the page after a pointer press, then after a key press', () => {
    stop = trackInputModality(document)

    document.body.dispatchEvent(new Event('pointerdown', { bubbles: true }))
    expect(document.documentElement.dataset.input).toBe('pointer')

    document.body.dispatchEvent(
      new KeyboardEvent('keydown', { key: 'Tab', bubbles: true }),
    )
    expect(document.documentElement.dataset.input).toBe('keyboard')
  })

  it('stops listening when asked', () => {
    stop = trackInputModality(document)
    stop()

    document.body.dispatchEvent(new Event('pointerdown', { bubbles: true }))
    expect(document.documentElement.dataset.input).toBeUndefined()
  })
})
