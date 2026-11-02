import { act, fireEvent, screen, waitFor, within } from '@testing-library/react'
import { describe, expect, it, vi } from 'vitest'
import {
  emptyResponse,
  jsonResponse,
  sessionResponse,
  stubFetch,
  unauthenticatedResponse,
} from '@/test/fetch-stub'
import { renderAppAt, renderSignedInAt } from '@/test/render-app'

const adminExists = () => jsonResponse({ setupRequired: false })

const ENDED = 'Your session ended. Log in again to continue.'

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

/** Brings the tab back into view, the way switching back to it does. */
function showTab() {
  const visibility = vi
    .spyOn(document, 'visibilityState', 'get')
    .mockReturnValue('visible')
  act(() => {
    document.dispatchEvent(new Event('visibilitychange'))
  })
  visibility.mockRestore()
}

/** A fetch answer that waits until the test resolves it. */
function deferred() {
  let resolve!: (response: Response) => void
  const promise = new Promise<Response>((done) => {
    resolve = done
  })
  return { answer: () => promise, resolve }
}

function logOut() {
  fireEvent.click(screen.getByRole('button', { name: 'Log out' }))
}

describe('account and logout', () => {
  it('shows who is signed in', async () => {
    stubFetch(sessionResponse('bridger'))
    await renderAppAt('/')

    expect(screen.getByText('Signed in as bridger')).toBeTruthy()
  })

  it('the phone menu has the account panel', async () => {
    await renderSignedInAt('/')

    fireEvent.click(screen.getByRole('button', { name: 'Open menu' }))
    const dialog = await screen.findByRole('dialog', { name: 'Menu' })
    expect(within(dialog).getByText('Signed in as bridger')).toBeTruthy()
    expect(within(dialog).getByRole('button', { name: 'Log out' })).toBeTruthy()
  })

  it('logs out and opens a plain login page', async () => {
    const { calls } = stubFetch(
      sessionResponse(),
      emptyResponse(204),
      adminExists(),
      unauthenticatedResponse(),
    )
    const router = await renderAppAt('/devices')

    logOut()

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    expect(calls[1]?.url).toBe('/api/auth/logout')
    expect(calls[1]?.init.method).toBe('POST')
    expect(router.state.location.search).toEqual({})
    expect(screen.queryByText(ENDED)).toBeNull()
  })

  it('Back after logout asks again and shows login without the note', async () => {
    stubFetch(
      sessionResponse(),
      sessionResponse(),
      emptyResponse(204),
      adminExists(),
      unauthenticatedResponse(),
      ...loggedOut(),
    )
    const router = await renderAppAt('/settings')
    await act(() => router.navigate({ to: '/devices' }))
    logOut()
    await screen.findByRole('heading', { level: 1, name: 'Log in' })

    act(() => router.history.back())

    await waitFor(() =>
      expect(router.state.location.search).toEqual({ redirect: '/settings' }),
    )
    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    expect(screen.queryByRole('navigation', { name: 'Main' })).toBeNull()
    expect(screen.queryByText(ENDED)).toBeNull()
  })

  it('a page change during logout does not say the session ended', async () => {
    const logoutAnswer = deferred()
    stubFetch(
      sessionResponse(),
      logoutAnswer.answer,
      ...loggedOut(),
      adminExists(),
      unauthenticatedResponse(),
    )
    const router = await renderAppAt('/')

    logOut()
    await act(() => router.navigate({ to: '/settings' }))
    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    await act(async () => logoutAnswer.resolve(emptyResponse(204)))

    await waitFor(() => expect(router.state.location.pathname).toBe('/login'))
    expect(screen.queryByText(ENDED)).toBeNull()
  })

  it('closing the phone menu during logout still logs out', async () => {
    const logoutAnswer = deferred()
    stubFetch(
      sessionResponse(),
      logoutAnswer.answer,
      adminExists(),
      unauthenticatedResponse(),
    )
    const router = await renderAppAt('/')
    fireEvent.click(screen.getByRole('button', { name: 'Open menu' }))
    const dialog = await screen.findByRole('dialog', { name: 'Menu' })

    fireEvent.click(within(dialog).getByRole('button', { name: 'Log out' }))
    fireEvent.keyDown(dialog, { key: 'Escape' })
    await waitFor(() => expect(screen.queryByRole('dialog')).toBeNull())
    await act(async () => logoutAnswer.resolve(emptyResponse(204)))

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    expect(router.state.location.pathname).toBe('/login')
    expect(screen.queryByText(ENDED)).toBeNull()
  })

  it('stays signed in when logout fails', async () => {
    stubFetch(sessionResponse(), emptyResponse(502))
    const router = await renderAppAt('/')

    logOut()

    const alert = await screen.findByRole('alert')
    expect(alert.textContent).toContain("Can't reach the Mosaic server")
    expect(
      screen.getByRole('heading', { level: 1, name: 'Welcome' }),
    ).toBeTruthy()
    expect(router.state.location.pathname).toBe('/')
  })
})

describe('coming back to the tab', () => {
  it('opens login with the note when the session ended', async () => {
    stubFetch(sessionResponse(), unauthenticatedResponse(), ...loggedOut())
    const router = await renderAppAt('/devices')

    showTab()

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    expect(router.state.location.search).toEqual({
      redirect: '/devices',
      session: 'ended',
    })
    expect(screen.getByText(ENDED)).toBeTruthy()
  })

  it('leaves the page alone when the check fails', async () => {
    const { calls } = stubFetch(sessionResponse(), emptyResponse(502))
    const router = await renderAppAt('/')

    showTab()

    await waitFor(() => expect(calls).toHaveLength(2))
    await new Promise((resolve) => setTimeout(resolve, 0))
    expect(
      screen.getByRole('heading', { level: 1, name: 'Welcome' }),
    ).toBeTruthy()
    expect(screen.queryByRole('alert')).toBeNull()
    expect(router.state.location.pathname).toBe('/')
  })
})
