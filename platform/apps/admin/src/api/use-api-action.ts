import { useCallback, useEffect, useRef, useState } from 'react'
import { isApiError, type ApiError } from '@/api/errors'

export type ActionResult<T> =
  | { status: 'success'; data: T }
  | { status: 'error'; error: ApiError }
  | { status: 'cancelled' }

/**
 * Runs an API call from a form or button and tracks `pending` and `error`.
 *
 * - `run` never rejects, so a submit handler can `await` it and act on
 *   `status` without a try/catch.
 * - Starting a run cancels the one in flight, as do `reset` and unmounting.
 *   Only the latest run may change state; earlier ones resolve `cancelled`.
 * - An error that is not an `ApiError` is a bug in the app. It is thrown
 *   during the next render so the route's error boundary shows it.
 */
export function useApiAction<I, T>(
  action: (input: I, signal: AbortSignal) => Promise<T>,
): {
  run: (input: I) => Promise<ActionResult<T>>
  pending: boolean
  error: ApiError | null
  reset: () => void
} {
  const [pending, setPending] = useState(false)
  const [error, setError] = useState<ApiError | null>(null)
  const [bug, setBug] = useState<{ error: unknown } | null>(null)
  const actionRef = useRef(action)
  const controllerRef = useRef<AbortController | null>(null)
  const mountedRef = useRef(false)

  useEffect(() => {
    actionRef.current = action
  })

  useEffect(() => {
    mountedRef.current = true
    return () => {
      mountedRef.current = false
      controllerRef.current?.abort()
      controllerRef.current = null
    }
  }, [])

  const run = useCallback(async (input: I): Promise<ActionResult<T>> => {
    controllerRef.current?.abort()
    const controller = new AbortController()
    controllerRef.current = controller
    setPending(true)
    setError(null)
    const isLatest = () =>
      mountedRef.current && controllerRef.current === controller

    try {
      const data = await actionRef.current(input, controller.signal)
      if (!isLatest()) return { status: 'cancelled' }
      controllerRef.current = null
      setPending(false)
      return { status: 'success', data }
    } catch (caught) {
      if (!isLatest()) return { status: 'cancelled' }
      controllerRef.current = null
      setPending(false)
      if (isApiError(caught)) {
        setError(caught)
        return { status: 'error', error: caught }
      }
      setBug({ error: caught })
      return { status: 'cancelled' }
    }
  }, [])

  const reset = useCallback(() => {
    controllerRef.current?.abort()
    controllerRef.current = null
    setPending(false)
    setError(null)
  }, [])

  if (bug) throw bug.error
  return { run, pending, error, reset }
}
