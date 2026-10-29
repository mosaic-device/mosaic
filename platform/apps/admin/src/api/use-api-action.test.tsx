import { Component, StrictMode, type ReactNode } from 'react'
import { act, renderHook, screen } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { ApiError } from '@/api/errors'
import { useApiAction, type ActionResult } from '@/api/use-api-action'

type Deferred<T> = {
  promise: Promise<T>
  resolve: (value: T) => void
  reject: (reason: unknown) => void
}

function deferred<T>(): Deferred<T> {
  let resolve!: (value: T) => void
  let reject!: (reason: unknown) => void
  const promise = new Promise<T>((res, rej) => {
    resolve = res
    reject = rej
  })
  return { promise, resolve, reject }
}

/** An action whose calls the test settles by hand, keeping each signal. */
function controlledAction<T>() {
  const calls: Array<{ input: string; signal: AbortSignal } & Deferred<T>> = []
  const action = (input: string, signal: AbortSignal) => {
    const call = { input, signal, ...deferred<T>() }
    calls.push(call)
    return call.promise
  }
  return { action, calls }
}

const unauthenticated = new ApiError({
  kind: 'problem',
  status: 401,
  code: 'unauthenticated',
  title: 'Log in to continue.',
})

afterEach(() => {
  vi.restoreAllMocks()
})

describe('useApiAction', () => {
  it('is pending while the action runs', async () => {
    const { action, calls } = controlledAction<string>()
    const { result } = renderHook(() => useApiAction(action))
    expect(result.current.pending).toBe(false)

    let outcome!: Promise<ActionResult<string>>
    act(() => {
      outcome = result.current.run('a')
    })
    expect(result.current.pending).toBe(true)
    expect(result.current.error).toBeNull()

    await act(async () => calls[0]!.resolve('done'))
    await expect(outcome).resolves.toEqual({ status: 'success', data: 'done' })
    expect(result.current.pending).toBe(false)
  })

  it('keeps an API error for the form', async () => {
    const { action, calls } = controlledAction<string>()
    const { result } = renderHook(() => useApiAction(action))

    let outcome!: Promise<ActionResult<string>>
    act(() => {
      outcome = result.current.run('a')
    })
    await act(async () => calls[0]!.reject(unauthenticated))
    await expect(outcome).resolves.toEqual({
      status: 'error',
      error: unauthenticated,
    })
    expect(result.current.error).toBe(unauthenticated)
    expect(result.current.pending).toBe(false)

    act(() => {
      void result.current.run('b')
    })
    expect(result.current.error).toBeNull()
    expect(result.current.pending).toBe(true)
  })

  it('cancels the earlier run when a new one starts', async () => {
    const { action, calls } = controlledAction<string>()
    const { result } = renderHook(() => useApiAction(action))

    let first!: Promise<ActionResult<string>>
    let second!: Promise<ActionResult<string>>
    act(() => {
      first = result.current.run('a')
    })
    act(() => {
      second = result.current.run('b')
    })
    expect(calls[0]!.signal.aborted).toBe(true)
    expect(calls[1]!.signal.aborted).toBe(false)

    await act(async () => calls[1]!.resolve('second'))
    await act(async () => calls[0]!.resolve('first, too late'))
    await expect(first).resolves.toEqual({ status: 'cancelled' })
    await expect(second).resolves.toEqual({
      status: 'success',
      data: 'second',
    })
    expect(result.current.pending).toBe(false)
    expect(result.current.error).toBeNull()
  })

  it('aborts on unmount and ignores the late result', async () => {
    const consoleError = vi.spyOn(console, 'error')
    const { action, calls } = controlledAction<string>()
    const { result, unmount } = renderHook(() => useApiAction(action))

    let outcome!: Promise<ActionResult<string>>
    act(() => {
      outcome = result.current.run('a')
    })
    unmount()
    expect(calls[0]!.signal.aborted).toBe(true)

    calls[0]!.reject(unauthenticated)
    await expect(outcome).resolves.toEqual({ status: 'cancelled' })
    expect(consoleError).not.toHaveBeenCalled()
  })

  it('reset clears the error and stops a run', async () => {
    const { action, calls } = controlledAction<string>()
    const { result } = renderHook(() => useApiAction(action))

    act(() => {
      void result.current.run('a')
    })
    await act(async () => calls[0]!.reject(unauthenticated))
    expect(result.current.error).toBe(unauthenticated)

    let outcome!: Promise<ActionResult<string>>
    act(() => {
      outcome = result.current.run('b')
    })
    act(() => result.current.reset())
    expect(calls[1]!.signal.aborted).toBe(true)
    expect(result.current.pending).toBe(false)
    expect(result.current.error).toBeNull()

    await act(async () => calls[1]!.resolve('too late'))
    await expect(outcome).resolves.toEqual({ status: 'cancelled' })
    expect(result.current.pending).toBe(false)
  })

  it('survives StrictMode remounting', async () => {
    const { action, calls } = controlledAction<string>()
    const { result } = renderHook(() => useApiAction(action), {
      wrapper: StrictMode,
    })

    let outcome!: Promise<ActionResult<string>>
    act(() => {
      outcome = result.current.run('a')
    })
    await act(async () => calls[0]!.resolve('done'))
    await expect(outcome).resolves.toEqual({ status: 'success', data: 'done' })
    expect(result.current.pending).toBe(false)
  })

  it('keeps the same run and reset between renders', () => {
    const { action } = controlledAction<string>()
    const { result, rerender } = renderHook(() => useApiAction(action))
    const { run, reset } = result.current
    rerender()
    expect(result.current.run).toBe(run)
    expect(result.current.reset).toBe(reset)
  })

  it('sends bugs to the error boundary', async () => {
    vi.spyOn(console, 'error').mockImplementation(() => {})
    const { action, calls } = controlledAction<string>()
    const { result } = renderHook(() => useApiAction(action), {
      wrapper: Boundary,
    })

    let outcome!: Promise<ActionResult<string>>
    act(() => {
      outcome = result.current.run('a')
    })
    await act(async () => calls[0]!.reject(new TypeError('boom')))
    await expect(outcome).resolves.toEqual({ status: 'cancelled' })
    expect(screen.getByText('Boundary caught it')).toBeTruthy()
  })
})

class Boundary extends Component<{ children: ReactNode }, { failed: boolean }> {
  override state = { failed: false }

  static getDerivedStateFromError() {
    return { failed: true }
  }

  override render() {
    return this.state.failed ? <p>Boundary caught it</p> : this.props.children
  }
}
