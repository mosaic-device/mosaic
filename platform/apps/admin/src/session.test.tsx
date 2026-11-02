import { act, screen, within } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import {
  emptyResponse,
  jsonResponse,
  sessionResponse,
  stubFetch,
  unauthenticatedResponse,
} from '@/test/fetch-stub'
import { renderAppAt } from '@/test/render-app'

const adminExists = () => jsonResponse({ setupRequired: false })

/** The guard's 401, then the login page's own setup and session checks. */
const loggedOut = () => [
  unauthenticatedResponse(),
  adminExists(),
  unauthenticatedResponse(),
]

describe('session check', () => {
  it('restores a session on a direct load', async () => {
    const { calls } = stubFetch(sessionResponse())
    await renderAppAt('/devices')

    expect(
      screen.getByRole('heading', { level: 1, name: 'Devices' }),
    ).toBeTruthy()
    expect(screen.queryByRole('heading', { name: 'Log in' })).toBeNull()
    expect(calls[0]?.url).toBe('/api/auth/session')
  })

  it('sends a visitor without a session to login with a return path', async () => {
    stubFetch(...loggedOut())
    const router = await renderAppAt('/devices')

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    expect(router.state.location.pathname).toBe('/login')
    expect(router.state.location.search).toEqual({ redirect: '/devices' })
  })

  it('adds no return path for Home', async () => {
    stubFetch(...loggedOut())
    const router = await renderAppAt('/')

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    expect(router.state.location.search).toEqual({})
  })

  it('checks again on every move between pages', async () => {
    const { calls } = stubFetch(sessionResponse(), sessionResponse())
    const router = await renderAppAt('/')

    await act(() => router.navigate({ to: '/settings' }))

    expect(
      screen.getByRole('heading', { level: 1, name: 'Settings' }),
    ).toBeTruthy()
    expect(calls.map((call) => call.url)).toEqual([
      '/api/auth/session',
      '/api/auth/session',
    ])
  })

  it('marks a session that ended while signed in', async () => {
    stubFetch(sessionResponse(), ...loggedOut())
    const router = await renderAppAt('/')

    await act(() => router.navigate({ to: '/settings' }))

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    expect(router.state.location.search).toEqual({
      redirect: '/settings',
      session: 'ended',
    })
  })

  it('shows the server-down page without the shell', async () => {
    stubFetch(emptyResponse(502))
    await renderAppAt('/devices')

    const main = screen.getByRole('main')
    expect(
      within(main).getByRole('heading', { name: "This page didn't load" }),
    ).toBeTruthy()
    expect(within(main).getByText(/Can't reach the Mosaic server/)).toBeTruthy()
    expect(screen.queryByRole('navigation', { name: 'Main' })).toBeNull()
  })
})
