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

/** The page notice that says so, which takes focus. */
function removedMessage(): HTMLElement | null {
  return screen.queryByText('Mosaic 3 removed.')?.closest('p') ?? null
}

function actionsButton(name = 'Mosaic 3'): HTMLElement {
  return screen.getByRole('button', { name: `Actions for ${name}` })
}

/** Opens a row's menu from the keyboard and returns it. */
async function openActions(name = 'Mosaic 3'): Promise<HTMLElement> {
  fireEvent.keyDown(actionsButton(name), { key: 'Enter' })
  return screen.findByRole('menu')
}

/** Picks Remove from a row's menu and returns the confirmation dialog. */
async function startRemoving(name = 'Mosaic 3'): Promise<HTMLElement> {
  const menu = await openActions(name)
  fireEvent.click(within(menu).getByRole('menuitem', { name: 'Remove' }))
  return screen.findByRole('alertdialog', { name: `Remove ${name}?` })
}

/** Picks Send test from a row's menu. */
async function sendTest(name = 'Mosaic 3') {
  const menu = await openActions(name)
  fireEvent.click(within(menu).getByRole('menuitem', { name: 'Send test' }))
  await waitFor(() => expect(screen.queryByRole('menu')).toBeNull())
}

/** The row's own result region, which screen readers read out. */
function resultOf(name = 'Mosaic 3'): HTMLElement {
  const row = screen
    .getByRole('heading', { level: 2, name })
    .closest('li') as HTMLElement
  return within(row).getByRole('status')
}

/** A request that stays open until the test calls `answer`. */
function deferred() {
  let answer: (response: Response) => void = () => {}
  const reply = () =>
    new Promise<Response>((resolve) => {
      answer = resolve
    })
  return { reply, answer: (response: Response) => answer(response) }
}

const outcome = (value: string) => jsonResponse({ outcome: value })
const DIDNT_HEAR = "The app didn't hear whether it arrived."

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
    expect(
      one.getByRole('button', { name: 'Actions for Mosaic 3' }),
    ).toBeTruthy()

    const two = within(second!)
    expect(
      two.getByRole('heading', { level: 2, name: 'Mosaic 5' }),
    ).toBeTruthy()
    expect(two.getByText('Offline')).toBeTruthy()
    expect(second!.querySelector('time')?.getAttribute('dateTime')).toBe(
      offline.pairedAt,
    )
    expect(
      two.getByRole('button', { name: 'Actions for Mosaic 5' }),
    ).toBeTruthy()
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
    await waitFor(() => expect(document.activeElement).toBe(actionsButton()))
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

  it('dismisses the removed message', async () => {
    await openDevices(list(online, offline), emptyResponse(204))

    confirmRemoval(await startRemoving())
    await waitFor(() => expect(removedMessage()).not.toBeNull())

    fireEvent.click(screen.getByRole('button', { name: 'Dismiss message' }))

    expect(removedMessage()).toBeNull()
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

  it('puts focus back on Remove device after a failure', async () => {
    await openDevices(list(online), new TypeError('Failed to fetch'))
    const dialog = await startRemoving()

    confirmRemoval(dialog)

    await within(dialog).findByRole('alert')
    await waitFor(() =>
      expect(document.activeElement).toBe(
        within(dialog).getByRole('button', { name: 'Remove device' }),
      ),
    )
  })

  it("doesn't close on a click outside it", async () => {
    await openDevices(list(online))
    const dialog = await startRemoving()
    // Radix starts listening for outside presses after a tick.
    await act(() => new Promise((resolve) => setTimeout(resolve, 0)))

    // Radix waits for the click that follows the press.
    fireEvent.pointerDown(document.body)
    fireEvent.click(document.body)

    expect(screen.getByRole('alertdialog')).toBe(dialog)
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

  it('stays and says so when the session check then passes', async () => {
    await openDevices(
      list(online),
      unauthenticatedResponse(),
      sessionResponse(),
      list(online),
    )
    const dialog = await startRemoving()

    confirmRemoval(dialog)

    const alert = await within(dialog).findByRole('alert')
    expect(alert.textContent).toContain('Log in to continue.')
    expect(screen.getByRole('alertdialog')).toBe(dialog)
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

describe('sending a test notification', () => {
  afterEach(() => {
    vi.useRealTimers()
  })

  it("offers it next to Remove in each device's menu", async () => {
    await openDevices(list(online, offline))

    const menu = await openActions('Mosaic 5')

    expect(
      within(menu)
        .getAllByRole('menuitem')
        .map((item) => item.textContent),
    ).toEqual(['Send test', 'Remove'])
  })

  it('waits for the device, then says it was delivered', async () => {
    const send = deferred()
    const { calls } = await openDevices(
      list(online, offline),
      send.reply,
      list(online, offline),
    )

    await sendTest()

    expect(calls[2]?.url).toBe('/api/devices/3/test-notification')
    expect(calls[2]?.init.method).toBe('POST')
    expect(calls[2]?.init.body).toBeUndefined()
    await waitFor(() =>
      expect(resultOf().textContent).toBe('Waiting for Mosaic 3 to answer…'),
    )
    const menu = await openActions()
    const sending = within(menu).getByRole('menuitem', { name: 'Sending…' })
    expect(sending.getAttribute('aria-disabled')).toBe('true')
    fireEvent.click(sending)
    expect(calls).toHaveLength(3)
    fireEvent.keyDown(menu, { key: 'Escape' })

    await act(async () => send.answer(outcome('delivered')))

    await waitFor(() =>
      expect(resultOf().textContent).toBe(
        'Delivered. Mosaic 3 confirmed it got the test notification.',
      ),
    )
    expect(resultOf('Mosaic 5').textContent).toBe('')
  })

  it.each([
    [
      'offline',
      "Not sent. Mosaic 3 is offline. Check that it's on and connected to Wi-Fi.",
    ],
    [
      'no_answer',
      "No answer. Mosaic 3 didn't confirm within 10 seconds, so it may not have arrived.",
    ],
    [
      'rejected',
      'Mosaic 3 refused the test notification. This is a server problem, and the server logged it.',
    ],
  ])('says what happened when the outcome is %s', async (value, text) => {
    await openDevices(list(online), outcome(value), list(online))

    await sendTest()

    await waitFor(() => expect(resultOf().textContent).toBe(text))
  })

  it('checks the list again so Online and Offline agree with the result', async () => {
    const { calls } = await openDevices(
      list(online),
      outcome('offline'),
      list({ ...online, connected: false }),
    )

    await sendTest()

    await waitFor(() => expect(calls[3]?.url).toBe('/api/devices'))
    await waitFor(() =>
      expect(within(rows()[0]!).getByText('Offline')).toBeTruthy(),
    )
  })

  it('keeps the result when the list refreshes', async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true })
    await openDevices(
      list(online),
      outcome('delivered'),
      list(online),
      list(online),
    )

    await sendTest()
    await waitFor(() => expect(resultOf().textContent).toMatch(/^Delivered/))

    await act(() => vi.advanceTimersByTimeAsync(REFRESH_MS))

    expect(resultOf().textContent).toMatch(/^Delivered/)
  })

  it("says when the app didn't hear back, since it may have arrived", async () => {
    await openDevices(list(online), new TypeError('Failed to fetch'))

    await sendTest()

    const row = rows()[0]!
    const alert = await within(row).findByRole('alert')
    expect(alert.textContent).toContain("Can't reach the Mosaic server.")
    expect(alert.textContent).toContain(DIDNT_HEAR)
    expect(resultOf().textContent).toBe('')
  })

  it('shows other refusals without saying it may have arrived', async () => {
    await openDevices(
      list(online),
      problemResponse({
        status: 403,
        code: 'forbidden_origin',
        title: 'This request did not come from the admin app.',
      }),
    )

    await sendTest()

    const alert = await within(rows()[0]!).findByRole('alert')
    expect(alert.textContent).toContain(
      'This request did not come from the admin app.',
    )
    expect(alert.textContent).not.toContain(DIDNT_HEAR)
  })

  it('waits out another test that is still waiting, then clears', async () => {
    vi.useFakeTimers({ shouldAdvanceTime: true })
    await openDevices(
      list(online),
      problemResponse(
        {
          status: 429,
          code: 'too_many_requests',
          title: 'Too many attempts. Wait a moment and try again.',
        },
        { 'Retry-After': '3' },
      ),
    )

    await sendTest()

    await waitFor(() =>
      expect(resultOf().textContent).toBe(
        'Another test notification to Mosaic 3 is still waiting for an answer.',
      ),
    )
    let menu = await openActions()
    const waiting = within(menu).getByRole('menuitem', {
      name: 'Try again in 3 seconds',
    })
    expect(waiting.getAttribute('aria-disabled')).toBe('true')
    fireEvent.keyDown(menu, { key: 'Escape' })
    await waitFor(() => expect(screen.queryByRole('menu')).toBeNull())

    await act(() => vi.advanceTimersByTimeAsync(3000))

    // The other test is over, so the banner no longer says it's waiting.
    expect(resultOf().textContent).toBe('')
    menu = await openActions()
    expect(
      within(menu)
        .getByRole('menuitem', { name: 'Send test' })
        .getAttribute('aria-disabled'),
    ).toBeNull()
  })

  it.each([
    ['no Retry-After', {}, 'Try again in 10 seconds'],
    ['a Retry-After of 0', { 'Retry-After': '0' }, 'Try again in 1 second'],
    [
      'a Retry-After of 600',
      { 'Retry-After': '600' },
      'Try again in 10 seconds',
    ],
  ])(
    'waits 1 to 10 seconds after a busy answer with %s',
    async (_, headers, label) => {
      await openDevices(
        list(online),
        problemResponse(
          {
            status: 429,
            code: 'too_many_requests',
            title: 'Too many attempts. Wait a moment and try again.',
          },
          headers,
        ),
      )

      await sendTest()

      await waitFor(() =>
        expect(resultOf().textContent).toBe(
          'Another test notification to Mosaic 3 is still waiting for an answer.',
        ),
      )
      const menu = await openActions()
      expect(within(menu).getByRole('menuitem', { name: label })).toBeTruthy()
    },
  )

  it('shows the empty state when the last device was removed elsewhere', async () => {
    await openDevices(list(online), notFound())

    await sendTest()

    await screen.findByRole('heading', { level: 2, name: 'No devices yet' })
    expect(
      screen.getByText("Mosaic 3 isn't paired anymore, so nothing was sent."),
    ).toBeTruthy()
  })

  it('stays and says so when the session check then passes', async () => {
    await openDevices(
      list(online),
      unauthenticatedResponse(),
      sessionResponse(),
      list(online),
    )

    await sendTest()

    const alert = await within(rows()[0]!).findByRole('alert')
    expect(alert.textContent).toContain('Log in to continue.')
  })

  it('drops a device removed elsewhere and says nothing was sent', async () => {
    await openDevices(list(online, offline), notFound())

    await sendTest()

    const message = (
      await screen.findByText(
        "Mosaic 3 isn't paired anymore, so nothing was sent.",
      )
    ).closest('p')
    expect(
      rows().map((row) => within(row).getByRole('heading').textContent),
    ).toEqual(['Mosaic 5'])
    await waitFor(() => expect(document.activeElement).toBe(message))
  })

  it('sends an ended session to login and back here', async () => {
    const { router } = await openDevices(
      list(online),
      unauthenticatedResponse(),
      unauthenticatedResponse(),
      jsonResponse({ setupRequired: false }),
      unauthenticatedResponse(),
    )

    await sendTest()

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    await waitFor(() =>
      expect(router.state.location.search).toEqual({
        redirect: '/devices',
        session: 'ended',
      }),
    )
  })

  it('dismisses a result and puts focus back on the menu button', async () => {
    await openDevices(list(online), outcome('delivered'), list(online))

    await sendTest()
    await waitFor(() => expect(resultOf().textContent).toMatch(/^Delivered/))

    fireEvent.click(
      screen.getByRole('button', { name: 'Dismiss result for Mosaic 3' }),
    )

    expect(resultOf().textContent).toBe('')
    expect(document.activeElement).toBe(actionsButton())
  })

  it('dismisses a failure too', async () => {
    await openDevices(list(online), new TypeError('Failed to fetch'))

    await sendTest()
    await within(rows()[0]!).findByRole('alert')

    fireEvent.click(
      screen.getByRole('button', { name: 'Dismiss result for Mosaic 3' }),
    )

    expect(within(rows()[0]!).queryByRole('alert')).toBeNull()
    expect(document.activeElement).toBe(actionsButton())
  })

  it('has nothing to dismiss while it waits', async () => {
    const send = deferred()
    await openDevices(list(online), send.reply, list(online))

    await sendTest()
    await waitFor(() =>
      expect(resultOf().textContent).toBe('Waiting for Mosaic 3 to answer…'),
    )

    expect(
      screen.queryByRole('button', { name: 'Dismiss result for Mosaic 3' }),
    ).toBeNull()
    await act(async () => send.answer(outcome('offline')))
  })

  it('dismisses the message about a device removed elsewhere', async () => {
    await openDevices(list(online, offline), notFound())

    await sendTest()
    await screen.findByText(
      "Mosaic 3 isn't paired anymore, so nothing was sent.",
    )

    fireEvent.click(screen.getByRole('button', { name: 'Dismiss message' }))

    expect(
      screen.queryByText("Mosaic 3 isn't paired anymore, so nothing was sent."),
    ).toBeNull()
    expect(document.activeElement).toBe(
      within(screen.getByRole('main')).getByRole('link', {
        name: 'Pair a device',
      }),
    )
  })

  it('lets the device be removed while a test waits', async () => {
    const send = deferred()
    await openDevices(list(online, offline), send.reply, emptyResponse(204))

    await sendTest()
    await waitFor(() =>
      expect(resultOf().textContent).toBe('Waiting for Mosaic 3 to answer…'),
    )
    confirmRemoval(await startRemoving())

    await waitFor(() => expect(removedMessage()).not.toBeNull())
    await act(async () => send.answer(outcome('no_answer')))
    expect(
      rows().map((row) => within(row).getByRole('heading').textContent),
    ).toEqual(['Mosaic 5'])
  })
})
