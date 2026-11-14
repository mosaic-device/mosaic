import { act, renderHook } from '@testing-library/react'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { ApiError } from '@/api/errors'
import { REFRESH_MS, useDeviceList } from '@/lib/use-device-list'
import {
  jsonResponse,
  stubFetch,
  unauthenticatedResponse,
} from '@/test/fetch-stub'

const online = { id: 3, pairedAt: '2026-11-06T18:30:00Z', connected: true }
const offline = { id: 5, pairedAt: '2026-11-14T20:12:00Z', connected: false }
const list = (...devices: object[]) => jsonResponse({ devices })
const LOADED_AT = Date.parse('2026-11-14T21:00:00Z')

function setVisibility(state: DocumentVisibilityState) {
  vi.spyOn(document, 'visibilityState', 'get').mockReturnValue(state)
  document.dispatchEvent(new Event('visibilitychange'))
}

function renderList(onUnauthenticated = vi.fn()) {
  const view = renderHook(() =>
    useDeviceList(
      { devices: [online, offline], loadedAt: LOADED_AT },
      onUnauthenticated,
    ),
  )
  return { ...view, onUnauthenticated }
}

async function wait(ms: number) {
  await act(() => vi.advanceTimersByTimeAsync(ms))
}

describe('useDeviceList', () => {
  beforeEach(() => {
    vi.useFakeTimers({ now: LOADED_AT })
  })
  afterEach(() => {
    vi.useRealTimers()
    vi.restoreAllMocks()
  })

  it('starts with the loaded list', () => {
    const { calls } = stubFetch()
    const { result } = renderList()

    expect(result.current.devices).toEqual([online, offline])
    expect(result.current.reportedAt).toBe(LOADED_AT)
    expect(result.current.refreshError).toBeNull()
    expect(calls).toHaveLength(0)
  })

  it('asks the server again every 10 seconds', async () => {
    const { calls } = stubFetch(
      list({ ...online, connected: false }, offline),
      list(online, offline),
    )
    const { result } = renderList()

    await wait(REFRESH_MS - 1)
    expect(calls).toHaveLength(0)

    await wait(1)
    expect(calls.map((call) => call.url)).toEqual(['/api/devices'])
    expect(result.current.devices[0]?.connected).toBe(false)
    expect(result.current.reportedAt).toBe(LOADED_AT + REFRESH_MS)

    await wait(REFRESH_MS)
    expect(calls).toHaveLength(2)
    expect(result.current.devices[0]?.connected).toBe(true)
  })

  it('waits while the tab is hidden and asks as soon as it is back', async () => {
    const { calls } = stubFetch(list(online))
    const { result } = renderList()

    act(() => setVisibility('hidden'))
    await wait(REFRESH_MS * 3)
    expect(calls).toHaveLength(0)

    act(() => setVisibility('visible'))
    await wait(0)
    expect(calls).toHaveLength(1)
    expect(result.current.devices).toEqual([online])
  })

  it('keeps the list when a check fails, until one works again', async () => {
    stubFetch(new TypeError('Failed to fetch'), list(online))
    const { result } = renderList()

    await wait(REFRESH_MS)
    expect(result.current.refreshError).toBeInstanceOf(ApiError)
    expect((result.current.refreshError as ApiError).kind).toBe('unavailable')
    expect(result.current.devices).toEqual([online, offline])
    expect(result.current.reportedAt).toBe(LOADED_AT)

    await wait(REFRESH_MS)
    expect(result.current.refreshError).toBeNull()
    expect(result.current.devices).toEqual([online])
  })

  it('hands an ended session to the caller', async () => {
    stubFetch(unauthenticatedResponse())
    const { result, onUnauthenticated } = renderList()

    await wait(REFRESH_MS)

    expect(onUnauthenticated).toHaveBeenCalledOnce()
    expect(result.current.refreshError).toBeNull()
  })

  it('leaves out a removed device, even from a check that started before', async () => {
    let answer: (response: Response) => void = () => {}
    stubFetch(
      () =>
        new Promise<Response>((resolve) => {
          answer = resolve
        }),
    )
    const { result } = renderList()
    await wait(REFRESH_MS)

    act(() => result.current.forget(3))
    expect(result.current.devices).toEqual([offline])

    await act(async () => answer(list(online, offline)))
    expect(result.current.devices).toEqual([offline])
  })

  it('stops asking once the page is gone', async () => {
    let signal: AbortSignal | null | undefined
    const { calls } = stubFetch((call) => {
      signal = call.init.signal
      return new Promise<Response>(() => {})
    })
    const { unmount } = renderList()
    await wait(REFRESH_MS)

    unmount()
    await wait(REFRESH_MS * 3)

    expect(signal?.aborted).toBe(true)
    expect(calls).toHaveLength(1)
  })
})
