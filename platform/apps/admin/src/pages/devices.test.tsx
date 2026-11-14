import { act, fireEvent, screen, waitFor, within } from '@testing-library/react'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { REFRESH_MS } from '@/lib/use-device-list'
import {
  emptyResponse,
  jsonResponse,
  problemResponse,
  sessionResponse,
  stubFetch,
  unauthenticatedResponse,
} from '@/test/fetch-stub'
import { renderAppAt } from '@/test/render-app'

// Examples from the Admin API Contract, section 5.
const online = { id: 3, pairedAt: '2026-11-06T18:30:00Z', connected: true }
const offline = { id: 5, pairedAt: '2026-11-14T20:12:00Z', connected: false }
const list = (...devices: object[]) => jsonResponse({ devices })
const notFound = () =>
  problemResponse({
    status: 404,
    code: 'device_not_found',
    title: "That device isn't paired with this server.",
  })
const NOT_CONFIRMED = "The server didn't confirm the removal. Try again."

/** Opens Devices signed in, then answers the given requests in order. */
async function openDevices(...answers: Parameters<typeof stubFetch>) {
  const stub = stubFetch(sessionResponse(), ...answers)
  const router = await renderAppAt('/devices')
  await screen.findByRole('heading', { level: 1, name: 'Devices' })
  return { ...stub, router }
}

function removedMessage(): HTMLElement | null {
  return screen.queryByText('Mosaic 3 removed.')
}

/** Clicks a row's Remove button and returns the confirmation dialog. */
async function startRemoving(name = 'Mosaic 3'): Promise<HTMLElement> {
  fireEvent.click(screen.getByRole('button', { name: `Remove ${name}` }))
  return screen.findByRole('alertdialog', { name: `Remove ${name}?` })
}

function confirmRemoval(dialog: HTMLElement) {
  fireEvent.click(within(dialog).getByRole('button', { name: 'Remove device' }))
}

/** `hidden` finds them behind an open dialog, which hides the rest. */
function rows({ hidden = false } = {}): HTMLElement[] {
  return within(
    screen.getByRole('list', { name: 'Paired devices', hidden }),
  ).getAllByRole('listitem', { hidden })
}

describe('device list', () => {
  it('shows each device as paired, and as online or offline', async () => {
    const { calls } = await openDevices(list(online, offline))

    expect(calls[1]?.url).toBe('/api/devices')
    const [first, second] = rows()
    expect(rows()).toHaveLength(2)

    const one = within(first!)
    expect(
      one.getByRole('heading', { level: 2, name: 'Mosaic 3' }),
    ).toBeTruthy()
    expect(one.getByText('Online')).toBeTruthy()
    expect(first!.querySelector('time')?.getAttribute('dateTime')).toBe(
      online.pairedAt,
    )
    expect(first!.textContent).toMatch(/Paired \w/)
    expect(one.getByRole('button', { name: 'Remove Mosaic 3' })).toBeTruthy()

    const two = within(second!)
    expect(
      two.getByRole('heading', { level: 2, name: 'Mosaic 5' }),
    ).toBeTruthy()
    expect(two.getByText('Offline')).toBeTruthy()
    expect(second!.querySelector('time')?.getAttribute('dateTime')).toBe(
      offline.pairedAt,
    )
    expect(two.getByRole('button', { name: 'Remove Mosaic 5' })).toBeTruthy()
  })

  it('shows the empty state without devices', async () => {
    await openDevices(list())

    expect(
      screen.getByRole('heading', { level: 2, name: 'No devices yet' }),
    ).toBeTruthy()
    expect(screen.queryByRole('list', { name: 'Paired devices' })).toBeNull()
  })

  it('keeps the way to pair', async () => {
    await openDevices(list(online))

    expect(
      within(screen.getByRole('main'))
        .getByRole('link', { name: 'Pair a device' })
        .getAttribute('href'),
    ).toBe('/devices/pair')
  })

  it('shows the error page when the list does not load, then tries again', async () => {
    stubFetch(
      sessionResponse(),
      new TypeError('Failed to fetch'),
      sessionResponse(),
      list(online),
    )
    await renderAppAt('/devices')

    await screen.findByRole('heading', {
      level: 1,
      name: "This page didn't load",
    })
    expect(screen.getByRole('alert').textContent).toContain(
      "Can't reach the Mosaic server.",
    )

    fireEvent.click(screen.getByRole('button', { name: 'Try again' }))

    await screen.findByRole('heading', { level: 2, name: 'Mosaic 3' })
  })

  it('sends an ended session to login and back here', async () => {
    stubFetch(
      sessionResponse(),
      unauthenticatedResponse(),
      jsonResponse({ setupRequired: false }),
      unauthenticatedResponse(),
    )
    const router = await renderAppAt('/devices')

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    await waitFor(() =>
      expect(router.state.location.search).toEqual({
        redirect: '/devices',
        session: 'ended',
      }),
    )
  })
})

describe('keeping the list current', () => {
  afterEach(() => {
    vi.useRealTimers()
  })

  it('says when a check fails and keeps what the server last reported', async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true })
    await openDevices(list(online), new TypeError('Failed to fetch'))

    await act(() => vi.advanceTimersByTimeAsync(REFRESH_MS))

    const alert = await screen.findByRole('alert')
    expect(alert.textContent).toContain("Can't reach the Mosaic server.")
    expect(alert.textContent).toMatch(
      /Online and offline show what the server last reported, at \S/,
    )
    expect(rows()).toHaveLength(1)
  })

  it('sends a session that ends meanwhile to login', async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true })
    const { router } = await openDevices(
      list(online),
      unauthenticatedResponse(),
      unauthenticatedResponse(),
      jsonResponse({ setupRequired: false }),
      unauthenticatedResponse(),
    )

    await act(() => vi.advanceTimersByTimeAsync(REFRESH_MS))

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    await waitFor(() =>
      expect(router.state.location.search).toEqual({
        redirect: '/devices',
        session: 'ended',
      }),
    )
  })
})

describe('removing a device', () => {
  it('asks first, and Cancel keeps the device', async () => {
    const { calls } = await openDevices(list(online, offline))

    const dialog = await startRemoving()

    expect(dialog.textContent).toContain(
      "It disconnects and stops working with this server right away, even if it's offline now. To use it again, pair it with the new code it shows.",
    )
    await waitFor(() =>
      expect(document.activeElement).toBe(
        within(dialog).getByRole('button', { name: 'Cancel' }),
      ),
    )

    fireEvent.click(within(dialog).getByRole('button', { name: 'Cancel' }))

    await waitFor(() => expect(screen.queryByRole('alertdialog')).toBeNull())
    await waitFor(() =>
      expect(document.activeElement).toBe(
        screen.getByRole('button', { name: 'Remove Mosaic 3' }),
      ),
    )
    expect(rows()).toHaveLength(2)
    expect(calls).toHaveLength(2)
  })

  it('removes the device and says so', async () => {
    const { calls } = await openDevices(
      list(online, offline),
      emptyResponse(204),
    )

    confirmRemoval(await startRemoving())

    await waitFor(() => expect(removedMessage()).not.toBeNull())
    expect(screen.queryByRole('alertdialog')).toBeNull()
    expect(calls[2]?.url).toBe('/api/devices/3')
    expect(calls[2]?.init.method).toBe('DELETE')
    expect(
      rows().map((row) => within(row).getByRole('heading').textContent),
    ).toEqual(['Mosaic 5'])
    await waitFor(() => expect(document.activeElement).toBe(removedMessage()))
  })

  it('counts a device already removed elsewhere as removed', async () => {
    await openDevices(list(online, offline), notFound())

    confirmRemoval(await startRemoving())

    await waitFor(() => expect(removedMessage()).not.toBeNull())
    expect(screen.queryByRole('alertdialog')).toBeNull()
    expect(screen.queryByRole('heading', { name: 'Mosaic 3' })).toBeNull()
  })

  it('shows the empty state after the last device', async () => {
    await openDevices(list(online), emptyResponse(204))

    confirmRemoval(await startRemoving())

    await screen.findByRole('heading', { level: 2, name: 'No devices yet' })
    expect(removedMessage()).not.toBeNull()
  })

  it("stays open when the server doesn't confirm, and a retry works", async () => {
    await openDevices(
      list(online, offline),
      new TypeError('Failed to fetch'),
      emptyResponse(204),
    )
    const dialog = await startRemoving()

    confirmRemoval(dialog)

    const alert = await within(dialog).findByRole('alert')
    expect(alert.textContent).toContain("Can't reach the Mosaic server.")
    expect(alert.textContent).toContain(NOT_CONFIRMED)
    expect(rows({ hidden: true })).toHaveLength(2)

    confirmRemoval(dialog)

    await waitFor(() => expect(removedMessage()).not.toBeNull())
  })

  it('shows other refusals without the retry advice', async () => {
    await openDevices(
      list(online),
      problemResponse({
        status: 403,
        code: 'forbidden_origin',
        title: 'This request did not come from the admin app.',
      }),
    )
    const dialog = await startRemoving()

    confirmRemoval(dialog)

    const alert = await within(dialog).findByRole('alert')
    expect(alert.textContent).toContain(
      'This request did not come from the admin app.',
    )
    expect(alert.textContent).not.toContain(NOT_CONFIRMED)
  })

  it('waits for the answer before anything else can happen', async () => {
    let answer: (response: Response) => void = () => {}
    const { calls } = await openDevices(
      list(online),
      () =>
        new Promise<Response>((resolve) => {
          answer = resolve
        }),
    )
    const dialog = await startRemoving()

    confirmRemoval(dialog)

    const pending = await within(dialog).findByRole('button', {
      name: 'Removing…',
    })
    expect((pending as HTMLButtonElement).disabled).toBe(true)
    const cancel = within(dialog).getByRole('button', { name: 'Cancel' })
    expect((cancel as HTMLButtonElement).disabled).toBe(true)
    fireEvent.keyDown(dialog, { key: 'Escape' })
    expect(screen.getByRole('alertdialog')).toBe(dialog)
    fireEvent.click(pending)
    expect(calls).toHaveLength(3)

    await act(async () => answer(emptyResponse(204)))

    await waitFor(() => expect(removedMessage()).not.toBeNull())
  })

  it('sends an ended session to login and back here', async () => {
    const { router } = await openDevices(
      list(online),
      unauthenticatedResponse(),
      unauthenticatedResponse(),
      jsonResponse({ setupRequired: false }),
      unauthenticatedResponse(),
    )

    confirmRemoval(await startRemoving())

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    await waitFor(() =>
      expect(router.state.location.search).toEqual({
        redirect: '/devices',
        session: 'ended',
      }),
    )
  })
})
