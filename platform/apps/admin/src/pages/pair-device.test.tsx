import { fireEvent, screen, waitFor, within } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import {
  jsonResponse,
  problemResponse,
  sessionResponse,
  stubFetch,
  textResponse,
  unauthenticatedResponse,
} from '@/test/fetch-stub'
import { renderAppAt } from '@/test/render-app'

const device = { id: 3, pairedAt: '2026-11-06T18:30:00Z', connected: true }
const claimed = (connected = true) =>
  jsonResponse({ device: { ...device, connected } })
const CODE_MESSAGE = 'Enter the 8-character code shown on the device.'
const NOT_CONFIRMED =
  "The server didn't confirm the pairing. Try again with the same code; it won't pair the device twice."

function codeField(): HTMLInputElement {
  return screen.getByLabelText('Pairing code')
}

function enterCode(value: string) {
  fireEvent.change(codeField(), { target: { value } })
  fireEvent.click(screen.getByRole('button', { name: 'Pair device' }))
}

/** Opens the pair page signed in, then answers the given requests in order. */
async function openPairPage(...answers: Parameters<typeof stubFetch>) {
  const stub = stubFetch(sessionResponse(), ...answers)
  const router = await renderAppAt('/devices/pair')
  await screen.findByRole('heading', { level: 1, name: 'Pair a device' })
  return { ...stub, router }
}

describe('getting to the pair page', () => {
  it('Devices links to it', async () => {
    stubFetch(sessionResponse(), sessionResponse())
    await renderAppAt('/devices')
    const main = screen.getByRole('main')

    fireEvent.click(within(main).getByRole('link', { name: 'Pair a device' }))

    await screen.findByRole('heading', { level: 1, name: 'Pair a device' })
  })

  it('keeps Devices current in the navigation', async () => {
    await openPairPage()
    const nav = screen.getByRole('navigation', { name: 'Main' })

    expect(
      within(nav)
        .getByRole('link', { name: 'Devices' })
        .getAttribute('aria-current'),
    ).toBe('page')
    expect(
      within(nav)
        .getByRole('link', { name: 'Home' })
        .getAttribute('aria-current'),
    ).toBeNull()
  })
})

describe('pair page', () => {
  it('asks for the code shown on the device', async () => {
    await openPairPage()

    expect(document.title).toBe('Pair a device · Mosaic Admin')
    const field = codeField()
    expect(field.getAttribute('autocomplete')).toBe('off')
    expect(field.getAttribute('autocapitalize')).toBe('characters')
    expect(field.getAttribute('spellcheck')).toBe('false')
    expect(field.required).toBe(true)
    expect(screen.getByText(/for example K7QF-2M9X/)).toBeTruthy()
  })

  it('pairs with a lower-case code without the dash', async () => {
    const { calls } = await openPairPage(claimed())

    enterCode('k7qf2m9x')

    const heading = await screen.findByRole('heading', {
      level: 2,
      name: 'Mosaic paired',
    })
    expect(calls[1]?.url).toBe('/api/devices/claim')
    expect(JSON.parse(String(calls[1]?.init.body))).toEqual({
      code: 'k7qf2m9x',
    })
    expect(document.activeElement).toBe(heading)
    expect(screen.getByText('Online')).toBeTruthy()
    // The device drawing also says "Paired"; the date line is the paragraph.
    expect(
      screen.getByText(/^Paired/, { selector: 'p' }).querySelector('time')
        ?.dateTime,
    ).toBe('2026-11-06T18:30:00Z')
    expect(screen.queryByLabelText('Pairing code')).toBeNull()
  })

  it('shows a device claimed while offline as paired and offline', async () => {
    await openPairPage(claimed(false))

    enterCode('K7QF-2M9X')

    await screen.findByRole('heading', { level: 2, name: 'Mosaic paired' })
    expect(screen.getByText('Offline')).toBeTruthy()
    expect(
      screen.getByText(/finishes pairing the next time it connects/),
    ).toBeTruthy()
  })

  it('links back to Devices and can pair another', async () => {
    await openPairPage(claimed())
    enterCode('K7QF-2M9X')
    await screen.findByRole('heading', { level: 2, name: 'Mosaic paired' })

    expect(
      screen.getByRole('link', { name: 'Go to devices' }).getAttribute('href'),
    ).toBe('/devices')
    fireEvent.click(screen.getByRole('button', { name: 'Pair another device' }))

    expect(codeField().value).toBe('')
    expect(document.activeElement).toBe(codeField())
  })

  it('points at a badly formed code without asking the server', async () => {
    const { calls } = await openPairPage()

    enterCode('K7QF-2M9')

    const field = codeField()
    expect(field.getAttribute('aria-invalid')).toBe('true')
    expect(screen.getByText(CODE_MESSAGE)).toBeTruthy()
    expect(document.activeElement).toBe(field)
    expect(calls).toHaveLength(1)
  })

  it('clears the message when the code changes', async () => {
    await openPairPage()
    enterCode('')
    expect(screen.getByText(CODE_MESSAGE)).toBeTruthy()

    fireEvent.change(codeField(), { target: { value: 'K' } })

    expect(screen.queryByText(CODE_MESSAGE)).toBeNull()
    expect(codeField().getAttribute('aria-invalid')).toBeNull()
  })

  it('shows the server field message under the code', async () => {
    await openPairPage(
      problemResponse({
        status: 400,
        code: 'validation_failed',
        title: 'Some fields need attention.',
        errors: { code: [CODE_MESSAGE] },
      }),
    )

    enterCode('K7QF-2M9X')

    await screen.findByText(CODE_MESSAGE)
    expect(codeField().getAttribute('aria-invalid')).toBe('true')
  })

  it.each([
    [
      404,
      'invalid_pairing_code',
      'No device is showing that code; check it and try again.',
    ],
    [
      410,
      'pairing_code_expired',
      'That code has expired; enter the one the device shows now.',
    ],
    [
      409,
      'pairing_code_used',
      'That code was already used; enter the new code the device shows.',
    ],
  ])('a %i %s says what to do under the code', async (status, code, title) => {
    await openPairPage(problemResponse({ status, code, title }))

    enterCode('K7QF-2M9X')

    const message = await screen.findByText(title)
    const field = codeField()
    expect(field.getAttribute('aria-invalid')).toBe('true')
    expect(field.getAttribute('aria-describedby')).toContain(message.id)
    expect(field.value).toBe('K7QF-2M9X')
    expect(document.activeElement).toBe(field)
    expect([field.selectionStart, field.selectionEnd]).toEqual([0, 9])
    expect(screen.queryByRole('heading', { name: 'Mosaic paired' })).toBeNull()
  })

  it('announces a code problem after Enter in the field', async () => {
    const title = 'That code has expired; enter the one the device shows now.'
    await openPairPage(
      problemResponse({ status: 410, code: 'pairing_code_expired', title }),
    )
    const field = codeField()
    field.focus()
    fireEvent.change(field, { target: { value: 'K7QF-2M9X' } })

    fireEvent.submit(field.form!)

    const alert = await screen.findByRole('alert')
    expect(alert.textContent).toBe(title)
    expect(document.activeElement).toBe(field)
  })

  it('waits out too many attempts', async () => {
    await openPairPage(
      problemResponse(
        {
          status: 429,
          code: 'too_many_requests',
          title: 'Too many attempts. Wait a moment and try again.',
        },
        { 'Retry-After': '7' },
      ),
    )

    enterCode('K7QF-2M9X')

    await screen.findByText('Too many attempts. Wait a moment and try again.')
    const button = screen.getByRole('button', {
      name: 'Try again in 7 seconds',
    })
    expect((button as HTMLButtonElement).disabled).toBe(true)
    expect(screen.queryByText(NOT_CONFIRMED)).toBeNull()
  })

  it('sends nothing while the wait runs', async () => {
    const { calls } = await openPairPage(
      problemResponse(
        {
          status: 429,
          code: 'too_many_requests',
          title: 'Too many attempts. Wait a moment and try again.',
        },
        { 'Retry-After': '7' },
      ),
    )
    enterCode('K7QF-2M9X')
    await screen.findByRole('button', { name: 'Try again in 7 seconds' })

    fireEvent.submit(codeField().form!)

    expect(calls).toHaveLength(2)
  })

  it('does not suggest a refused claim went through', async () => {
    await openPairPage(
      problemResponse({
        status: 403,
        code: 'forbidden_origin',
        title: 'This request did not come from the admin app.',
      }),
    )

    enterCode('K7QF-2M9X')

    const alert = await screen.findByRole('alert')
    expect(alert.textContent).toContain(
      'This request did not come from the admin app.',
    )
    expect(screen.queryByText(NOT_CONFIRMED)).toBeNull()
  })

  it.each([
    ['an unreachable server', () => new TypeError('Failed to fetch')],
    [
      'the dev proxy without a server',
      () => textResponse('', 403, 'text/plain'),
    ],
    ['an unexpected answer', () => jsonResponse({ device: {} })],
    [
      'an internal error',
      () =>
        problemResponse({
          status: 500,
          code: 'internal_error',
          title: 'Something went wrong.',
        }),
    ],
  ])('never implies success after %s', async (_, answer) => {
    await openPairPage(answer())

    enterCode('K7QF-2M9X')

    const alert = await screen.findByRole('alert')
    expect(within(alert).getByText(NOT_CONFIRMED)).toBeTruthy()
    expect(screen.queryByRole('heading', { name: 'Mosaic paired' })).toBeNull()
    expect(codeField().value).toBe('K7QF-2M9X')
  })

  it('stays on one claim while it waits', async () => {
    let answer: (response: Response) => void = () => {}
    const { calls } = await openPairPage(
      () => new Promise<Response>((resolve) => (answer = resolve)),
    )

    enterCode('K7QF-2M9X')
    const button = await screen.findByRole('button', { name: 'Pairing…' })
    expect((button as HTMLButtonElement).disabled).toBe(true)
    fireEvent.submit(codeField().form!)
    expect(calls).toHaveLength(2)

    answer(claimed())
    await screen.findByRole('heading', { level: 2, name: 'Mosaic paired' })
  })

  it('sends an ended session to login and back here', async () => {
    const { router } = await openPairPage(
      unauthenticatedResponse(),
      unauthenticatedResponse(),
      jsonResponse({ setupRequired: false }),
      unauthenticatedResponse(),
    )

    enterCode('K7QF-2M9X')

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    await waitFor(() =>
      expect(router.state.location.search).toEqual({
        redirect: '/devices/pair',
        session: 'ended',
      }),
    )
  })

  it('still says something when the session check then passes', async () => {
    await openPairPage(unauthenticatedResponse(), sessionResponse())

    enterCode('K7QF-2M9X')

    const alert = await screen.findByRole('alert')
    expect(alert.textContent).toContain('Log in to continue.')
    expect(screen.queryByRole('heading', { name: 'Mosaic paired' })).toBeNull()
  })
})

const STEPS = [
  'Plug in your Mosaic and finish its Wi-Fi setup.',
  "It shows an 8-character code and this server's address.",
  'Enter the code within 10 minutes. If it runs out, your Mosaic shows a new one.',
]
const help = (name: string) => screen.getByRole('complementary', { name })

describe('pairing help', () => {
  it('explains where the code comes from', async () => {
    await openPairPage()
    const items = within(help('Where to find the code')).getAllByRole(
      'listitem',
    )
    expect(items.map((item) => item.textContent)).toEqual(STEPS)
  })

  it('keeps the drawing out of the accessibility tree', async () => {
    await openPairPage()
    const sample = within(help('Where to find the code')).getByText('K7QF-2M9X')
    expect(sample.closest('[aria-hidden="true"]')).not.toBeNull()
  })

  it('puts the form before the help', async () => {
    await openPairPage()
    const position = codeField().compareDocumentPosition(
      help('Where to find the code'),
    )
    expect(position & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy()
  })

  it('shows the paired screen after a claim', async () => {
    await openPairPage(claimed())
    enterCode('K7QF-2M9X')
    const after = await screen.findByRole('complementary', {
      name: 'On your Mosaic',
    })
    expect(after.textContent).toContain(
      "Your Mosaic shows that it's paired, then opens its Home screen.",
    )
    expect(
      screen.queryByRole('complementary', { name: 'Where to find the code' }),
    ).toBeNull()

    fireEvent.click(screen.getByRole('button', { name: 'Pair another device' }))
    expect(help('Where to find the code')).toBeTruthy()
  })
})

describe('pairing help for an offline device', () => {
  it('says the device shows it is paired once it connects', async () => {
    await openPairPage(claimed(false))
    enterCode('K7QF-2M9X')
    const after = await screen.findByRole('complementary', {
      name: 'On your Mosaic',
    })
    expect(after.textContent).toContain(
      "When it next connects, your Mosaic shows that it's paired, then opens its Home screen.",
    )
  })
})

describe('leaving the pair page', () => {
  it('has a way back to Devices above the title', async () => {
    stubFetch(sessionResponse(), sessionResponse())
    const router = await renderAppAt('/devices/pair')
    await screen.findByRole('heading', { level: 1, name: 'Pair a device' })
    const main = screen.getByRole('main')

    const back = within(main).getByRole('link', { name: 'Back to Devices' })
    expect(back.getAttribute('href')).toBe('/devices')
    const position = back.compareDocumentPosition(
      within(main).getByRole('heading', { level: 1 }),
    )
    expect(position & Node.DOCUMENT_POSITION_FOLLOWING).toBeTruthy()

    fireEvent.click(back)

    await screen.findByRole('heading', { level: 1, name: 'Devices' })
    expect(router.state.location.pathname).toBe('/devices')
  })
})
