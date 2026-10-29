import { act, fireEvent, render, screen } from '@testing-library/react'
import {
  createMemoryHistory,
  createRootRoute,
  createRoute,
  createRouter,
  Outlet,
  RouterProvider,
} from '@tanstack/react-router'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { describeError } from '@/api/describe-error'
import { ApiError } from '@/api/errors'
import { ApiErrorNotice } from '@/components/ApiErrorNotice'
import { PageError } from '@/components/PageError'
import { createAppRouter } from '@/router'

const UNAVAILABLE =
  "Can't reach the Mosaic server. Check that it's running, then try again."
const MALFORMED = "The server sent a response the app didn't expect. Try again."
const BUG = 'Something went wrong on this page. Try again, or reload the page.'

const invalidCredentials = new ApiError({
  kind: 'problem',
  status: 401,
  code: 'invalid_credentials',
  title: 'Incorrect username or password.',
})
const internalError = new ApiError({
  kind: 'problem',
  status: 500,
  code: 'internal_error',
  title: 'Something went wrong.',
  traceId: '00-abc-01',
})
const unavailable = new ApiError({ kind: 'unavailable', status: 502 })
const malformed = new ApiError({ kind: 'malformed', status: 200 })

describe('describeError', () => {
  it('uses the server title for problems', () => {
    expect(describeError(invalidCredentials)).toEqual({
      message: 'Incorrect username or password.',
    })
  })

  it('adds the trace ID only for internal errors', () => {
    expect(describeError(internalError)).toEqual({
      message: 'Something went wrong.',
      reference: '00-abc-01',
    })
    const withTrace = new ApiError({
      kind: 'problem',
      status: 403,
      code: 'invalid_setup_code',
      title: 'The setup code is incorrect.',
      traceId: '00-def-01',
    })
    expect(describeError(withTrace).reference).toBeUndefined()
  })

  it('has plain wording for the other kinds and for bugs', () => {
    expect(describeError(unavailable)).toEqual({ message: UNAVAILABLE })
    expect(describeError(malformed)).toEqual({ message: MALFORMED })
    expect(describeError(new TypeError('boom'))).toEqual({ message: BUG })
  })
})

describe('ApiErrorNotice', () => {
  it('renders nothing without an error', () => {
    const { container } = render(<ApiErrorNotice error={null} />)
    expect(container.innerHTML).toBe('')
  })

  it('announces the message for each kind', () => {
    for (const [error, message] of [
      [invalidCredentials, 'Incorrect username or password.'],
      [unavailable, UNAVAILABLE],
      [malformed, MALFORMED],
    ] as const) {
      const { unmount } = render(<ApiErrorNotice error={error} />)
      expect(screen.getByRole('alert').textContent).toContain(message)
      expect(screen.queryByText(/Reference:/)).toBeNull()
      unmount()
    }
  })

  it('shows the reference for an internal error', () => {
    render(<ApiErrorNotice error={internalError} />)
    expect(screen.getByRole('alert').textContent).toContain(
      'Something went wrong.',
    )
    expect(screen.getByText('Reference: 00-abc-01')).toBeTruthy()
  })
})

describe('PageError', () => {
  beforeEach(() => {
    // The router and React log errors they catch; these tests expect them.
    vi.spyOn(console, 'error').mockImplementation(() => {})
    vi.spyOn(console, 'warn').mockImplementation(() => {})
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  async function renderRoute(
    options: Pick<Parameters<typeof createRoute>[0], 'loader' | 'component'>,
  ) {
    const rootRoute = createRootRoute({ component: Outlet })
    const indexRoute = createRoute({
      getParentRoute: () => rootRoute,
      path: '/',
      ...options,
    })
    const router = createRouter({
      routeTree: rootRoute.addChildren([indexRoute]),
      history: createMemoryHistory({ initialEntries: ['/'] }),
      defaultErrorComponent: PageError,
    })
    await act(() => router.load())
    render(<RouterProvider router={router} />)
  }

  it('shows a failed load and tries again', async () => {
    let loads = 0
    await renderRoute({
      loader: () => {
        loads += 1
        if (loads === 1) throw unavailable
        return 'ok'
      },
      component: () => <p>Loaded</p>,
    })

    expect(
      await screen.findByRole('heading', {
        level: 1,
        name: "This page didn't load",
      }),
    ).toBeTruthy()
    expect(screen.getByRole('alert').textContent).toContain(UNAVAILABLE)
    expect(document.title).toBe('Error · Mosaic Admin')

    fireEvent.click(screen.getByRole('button', { name: 'Try again' }))
    expect(await screen.findByText('Loaded')).toBeTruthy()
    expect(loads).toBe(2)
  })

  it('does not blame the server for a bug in a page', async () => {
    await renderRoute({
      component: () => {
        throw new TypeError('boom')
      },
    })
    expect(
      await screen.findByRole('heading', {
        level: 1,
        name: "This page didn't load",
      }),
    ).toBeTruthy()
    expect(screen.getByRole('alert').textContent).toContain(BUG)
  })

  it('is the app router default', () => {
    expect(createAppRouter().options.defaultErrorComponent).toBe(PageError)
  })
})
