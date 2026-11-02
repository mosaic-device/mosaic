import { act, renderHook } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { useCountdown } from '@/lib/use-countdown'

describe('useCountdown', () => {
  beforeEach(() => {
    vi.useFakeTimers()
  })
  afterEach(() => {
    vi.useRealTimers()
  })

  it('counts down whole seconds to zero', () => {
    const { result } = renderHook(() => useCountdown())
    expect(result.current.secondsLeft).toBe(0)

    act(() => result.current.start(3))
    expect(result.current.secondsLeft).toBe(3)

    act(() => vi.advanceTimersByTime(1000))
    expect(result.current.secondsLeft).toBe(2)

    act(() => vi.advanceTimersByTime(2000))
    expect(result.current.secondsLeft).toBe(0)
    expect(vi.getTimerCount()).toBe(0)
  })

  it('restarts from the new value', () => {
    const { result } = renderHook(() => useCountdown())
    act(() => result.current.start(5))
    act(() => vi.advanceTimersByTime(3000))
    act(() => result.current.start(10))
    expect(result.current.secondsLeft).toBe(10)
  })
})
