import {
  useCallback,
  useEffect,
  useEffectEvent,
  useMemo,
  useState,
} from 'react'
import { listDevices, type Device } from '@/api/devices'
import { isUnauthenticated } from '@/api/errors'

/** How often the list asks the server again while the tab is visible. */
export const REFRESH_MS = 10_000

export type LoadedDevices = { devices: Device[]; loadedAt: number }

/**
 * Keeps the device list current. The server never pushes changes, so this
 * asks again every 10 seconds while the tab is visible, and as soon as the
 * owner comes back to it (Admin API Contract, section 5).
 *
 * - A failed check keeps the last list and sets `refreshError` until a later
 *   check works; `reportedAt` says when the server last answered.
 * - A `401` goes to `onUnauthenticated`, which should send the owner to login.
 * - `forget(id)` drops a device removed on this page, also from a check that
 *   started before the removal. IDs are never reused, so this never hides a
 *   device paired later.
 * - A newer `loaded` (the route loaded again) replaces the list in place, so
 *   the page around it keeps its state.
 */
export function useDeviceList(
  loaded: LoadedDevices,
  onUnauthenticated: () => void,
): {
  devices: Device[]
  reportedAt: number
  refreshError: unknown
  forget: (id: number) => void
} {
  const [list, setList] = useState({
    devices: loaded.devices,
    reportedAt: loaded.loadedAt,
  })
  const [refreshError, setRefreshError] = useState<unknown>(null)
  const [removed, setRemoved] = useState<ReadonlySet<number>>(() => new Set())
  const [loadedAt, setLoadedAt] = useState(loaded.loadedAt)
  const sessionEnded = useEffectEvent(onUnauthenticated)

  if (loaded.loadedAt !== loadedAt) {
    setLoadedAt(loaded.loadedAt)
    setList({ devices: loaded.devices, reportedAt: loaded.loadedAt })
    setRefreshError(null)
  }

  useEffect(() => {
    let timer: ReturnType<typeof setTimeout> | undefined
    let controller: AbortController | undefined

    function scheduleNext() {
      clearTimeout(timer)
      if (document.visibilityState === 'visible') {
        timer = setTimeout(() => void check(), REFRESH_MS)
      }
    }

    async function check() {
      clearTimeout(timer)
      controller?.abort()
      const current = new AbortController()
      controller = current
      try {
        const { devices } = await listDevices(current.signal)
        // An answer that lands just as the check is cancelled is dropped.
        if (current.signal.aborted) return
        setList({ devices, reportedAt: Date.now() })
        setRefreshError(null)
      } catch (error) {
        // Cancelled by a newer check, a hidden tab or leaving the page.
        if (current.signal.aborted) return
        if (isUnauthenticated(error)) sessionEnded()
        else setRefreshError(error)
      }
      scheduleNext()
    }

    function onVisibilityChange() {
      if (document.visibilityState === 'visible') {
        void check()
      } else {
        clearTimeout(timer)
        controller?.abort()
      }
    }

    scheduleNext()
    document.addEventListener('visibilitychange', onVisibilityChange)
    return () => {
      document.removeEventListener('visibilitychange', onVisibilityChange)
      clearTimeout(timer)
      controller?.abort()
    }
  }, [])

  const devices = useMemo(
    () => list.devices.filter((device) => !removed.has(device.id)),
    [list.devices, removed],
  )
  const forget = useCallback((id: number) => {
    setRemoved((previous) => new Set(previous).add(id))
  }, [])

  return { devices, reportedAt: list.reportedAt, refreshError, forget }
}
