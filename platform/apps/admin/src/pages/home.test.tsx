import { act, screen, waitFor, within } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { REFRESH_MS } from '@/lib/use-device-list'
import {
  jsonResponse,
  sessionResponse,
  stubFetch,
  unauthenticatedResponse,
} from '@/test/fetch-stub'
import { renderAppAt } from '@/test/render-app'

// Examples from the Admin API Contract, section 5.
const online = { id: 3, pairedAt: '2026-11-06T18:30:00Z', connected: true }
const offline = { id: 5, pairedAt: '2026-11-14T20:12:00Z', connected: false }
const list = (...devices: object[]) => jsonResponse({ devices })

/** Opens Home signed in, then answers the given requests in order. */
async function openHome(...answers: Parameters<typeof stubFetch>) {
  const stub = stubFetch(sessionResponse(), ...answers)
  const router = await renderAppAt('/')
  await screen.findByRole('heading', { level: 1, name: 'Welcome' })
  return { ...stub, router }
}

/** Home's Device card. */
function deviceCard(): HTMLElement {
  return screen
    .getByRole('heading', { level: 2, name: 'Device' })
    .closest('[data-slot="card"]') as HTMLElement
}

describe("Home's Device card", () => {
  afterEach(() => {
    vi.useRealTimers()
  })

  it('says nothing is paired yet, with a way to pair', async () => {
    const { calls } = await openHome(list())

    expect(calls[1]?.url).toBe('/api/devices')
    const card = within(deviceCard())
    expect(card.getByText('Not paired')).toBeTruthy()
    expect(card.getByText('No device is paired with this server.')).toBeTruthy()
    expect(
      card.getByRole('link', { name: 'Pair a device' }).getAttribute('href'),
    ).toBe('/devices/pair')
  })

  it('shows one paired device as online, with a way to the list', async () => {
    await openHome(list(online))

    const card = within(deviceCard())
    expect(card.getByText('Online')).toBeTruthy()
    expect(deviceCard().textContent).toMatch(/Mosaic 3, paired \w.*\./)
    expect(deviceCard().querySelector('time')?.getAttribute('dateTime')).toBe(
      online.pairedAt,
    )
    expect(
      card.getByRole('link', { name: 'View devices' }).getAttribute('href'),
    ).toBe('/devices')
  })

  it('shows one paired device as offline', async () => {
    await openHome(list(offline))

    expect(within(deviceCard()).getByText('Offline')).toBeTruthy()
    expect(deviceCard().textContent).toContain('Mosaic 5, paired')
  })

  it('counts several devices and how many are online', async () => {
    await openHome(list(online, offline))

    const card = within(deviceCard())
    expect(card.getByText('1 of 2 online')).toBeTruthy()
    expect(
      card.getByText('2 devices are paired with this server.'),
    ).toBeTruthy()
    expect(card.getByRole('link', { name: 'View devices' })).toBeTruthy()
  })

  it('keeps Online and Offline current', async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true })
    await openHome(list(offline), list({ ...offline, connected: true }))

    expect(within(deviceCard()).getByText('Offline')).toBeTruthy()

    await act(() => vi.advanceTimersByTimeAsync(REFRESH_MS))

    await waitFor(() =>
      expect(within(deviceCard()).getByText('Online')).toBeTruthy(),
    )
  })

  it('sends an ended session to login, which returns to Home', async () => {
    stubFetch(
      sessionResponse(),
      unauthenticatedResponse(),
      jsonResponse({ setupRequired: false }),
      unauthenticatedResponse(),
    )
    const router = await renderAppAt('/')

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    await waitFor(() =>
      expect(router.state.location.search).toEqual({ session: 'ended' }),
    )
  })
})
