import { fireEvent, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import {
  emptyResponse,
  jsonResponse,
  problemResponse,
  stubFetch,
} from '@/test/fetch-stub'
import { renderAppAt } from '@/test/render-app'

const UNAVAILABLE =
  "Can't reach the Mosaic server. Check that it's running, then try again."
const ALREADY_SET_UP =
  'This server is already set up. Log in with its admin account.'

const noAdmin = () => jsonResponse({ setupRequired: true })
const adminExists = () => jsonResponse({ setupRequired: false })

function fill(label: string, value: string) {
  fireEvent.change(screen.getByLabelText(label), { target: { value } })
}

function fillValid() {
  fill('Username', ' bridger ')
  fill('Password', 'a-long-password')
  fill('Setup code', 'k7qf-2m9x')
}

function submit() {
  fireEvent.click(screen.getByRole('button', { name: 'Create account' }))
}

/** The text of the hint and error messages linked to a field. */
function descriptionOf(label: string): string {
  const ids = screen.getByLabelText(label).getAttribute('aria-describedby')
  return (ids ?? '')
    .split(' ')
    .map((id) => document.getElementById(id)?.textContent ?? '')
    .join(' ')
}

describe('setup page', () => {
  it('shows the setup form while no admin exists', async () => {
    const { calls } = stubFetch(noAdmin())
    await renderAppAt('/setup')

    await screen.findByRole('heading', { level: 1, name: 'Set up Mosaic' })
    expect(document.title).toBe('Set up · Mosaic Admin')
    expect(screen.getByLabelText('Username').getAttribute('autocomplete')).toBe(
      'username',
    )
    expect(screen.getByLabelText('Password').getAttribute('autocomplete')).toBe(
      'new-password',
    )
    expect(descriptionOf('Setup code')).toContain('Setup code')
    expect(calls.map((call) => call.url)).toEqual(['/api/setup'])
  })

  it('sends a visitor to login once an admin exists', async () => {
    stubFetch(adminExists(), adminExists())
    const router = await renderAppAt('/setup')

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    expect(router.state.location.pathname).toBe('/login')
    expect(screen.getByText(ALREADY_SET_UP)).toBeTruthy()
  })

  it('shows the error page when the server cannot be reached', async () => {
    stubFetch(new TypeError('Failed to fetch'), noAdmin())
    await renderAppAt('/setup')

    await screen.findByRole('heading', { name: "This page didn't load" })
    expect(screen.getByRole('alert').textContent).toBe(UNAVAILABLE)

    fireEvent.click(screen.getByRole('button', { name: 'Try again' }))
    await screen.findByRole('heading', { name: 'Set up Mosaic' })
  })

  it('checks the fields before sending anything', async () => {
    const { calls } = stubFetch(noAdmin())
    await renderAppAt('/setup')
    await screen.findByRole('heading', { name: 'Set up Mosaic' })

    fill('Username', 'ab')
    fill('Password', 'short')
    submit()

    expect(descriptionOf('Username')).toContain('Must be 3 to 32 characters.')
    expect(descriptionOf('Password')).toContain(
      'Must be at least 12 characters.',
    )
    expect(descriptionOf('Setup code')).toContain('Required.')
    expect(screen.getByLabelText('Username').getAttribute('aria-invalid')).toBe(
      'true',
    )
    expect(document.activeElement).toBe(screen.getByLabelText('Username'))
    expect(calls).toHaveLength(1)
  })

  it('clears a field message once that field changes', async () => {
    stubFetch(noAdmin())
    await renderAppAt('/setup')
    await screen.findByRole('heading', { name: 'Set up Mosaic' })

    submit()
    fill('Username', 'bridger')

    expect(descriptionOf('Username')).not.toContain('Required.')
    expect(descriptionOf('Password')).toContain('Required.')
  })

  it('creates the admin and opens Home', async () => {
    const { calls } = stubFetch(
      noAdmin(),
      jsonResponse({ admin: { username: 'bridger' } }, 201),
    )
    const router = await renderAppAt('/setup')
    await screen.findByRole('heading', { name: 'Set up Mosaic' })

    fillValid()
    submit()

    await screen.findByRole('heading', { level: 1, name: 'Welcome' })
    expect(router.state.location.pathname).toBe('/')
    expect(calls[1]?.url).toBe('/api/setup')
    expect(calls[1]?.init.method).toBe('POST')
    expect(JSON.parse(String(calls[1]?.init.body))).toEqual({
      username: 'bridger',
      password: 'a-long-password',
      setupCode: 'k7qf-2m9x',
    })
  })

  it('sends values a password manager filled in without an input event', async () => {
    const { calls } = stubFetch(
      noAdmin(),
      jsonResponse({ admin: { username: 'bridger' } }, 201),
    )
    await renderAppAt('/setup')
    await screen.findByRole('heading', { name: 'Set up Mosaic' })

    screen.getByLabelText<HTMLInputElement>('Username').value = 'bridger'
    screen.getByLabelText<HTMLInputElement>('Password').value =
      'generated-password'
    fill('Setup code', 'k7qf-2m9x')
    submit()

    await screen.findByRole('heading', { level: 1, name: 'Welcome' })
    expect(JSON.parse(String(calls[1]?.init.body))).toEqual({
      username: 'bridger',
      password: 'generated-password',
      setupCode: 'k7qf-2m9x',
    })
  })

  it('turns off autocorrect on the username and setup code', async () => {
    stubFetch(noAdmin())
    await renderAppAt('/setup')
    await screen.findByRole('heading', { name: 'Set up Mosaic' })

    for (const label of ['Username', 'Setup code']) {
      expect(screen.getByLabelText(label).getAttribute('autocorrect')).toBe(
        'off',
      )
    }
  })

  it('keeps the button disabled while the server answers', async () => {
    stubFetch(noAdmin(), () => new Promise<Response>(() => {}))
    await renderAppAt('/setup')
    await screen.findByRole('heading', { name: 'Set up Mosaic' })

    fillValid()
    submit()

    const button = await screen.findByRole('button', {
      name: 'Creating account…',
    })
    expect(button.hasAttribute('disabled')).toBe(true)
  })

  it('shows the server field messages under their fields', async () => {
    stubFetch(
      noAdmin(),
      problemResponse({
        status: 400,
        code: 'validation_failed',
        title: 'Some fields need attention.',
        traceId: '00-a-01',
        errors: { username: ['Use only letters, digits.'] },
      }),
    )
    await renderAppAt('/setup')
    await screen.findByRole('heading', { name: 'Set up Mosaic' })

    fillValid()
    submit()

    await screen.findByText('Use only letters, digits.')
    expect(descriptionOf('Username')).toContain('Use only letters, digits.')
    expect(screen.queryByRole('alert')).toBeNull()
  })

  it('puts a wrong setup code under its field', async () => {
    stubFetch(
      noAdmin(),
      problemResponse({
        status: 403,
        code: 'invalid_setup_code',
        title: 'The setup code is incorrect.',
        traceId: '00-b-01',
      }),
    )
    await renderAppAt('/setup')
    await screen.findByRole('heading', { name: 'Set up Mosaic' })

    fillValid()
    submit()

    await screen.findByText('The setup code is incorrect.')
    expect(descriptionOf('Setup code')).toContain(
      'The setup code is incorrect.',
    )
    expect(document.activeElement).toBe(screen.getByLabelText('Setup code'))
  })

  it('moves to login when someone already finished setup', async () => {
    stubFetch(
      noAdmin(),
      problemResponse({
        status: 409,
        code: 'setup_already_completed',
        title: 'Setup has already been completed.',
        traceId: '00-c-01',
      }),
      adminExists(),
    )
    const router = await renderAppAt('/setup')
    await screen.findByRole('heading', { name: 'Set up Mosaic' })

    fillValid()
    submit()

    await screen.findByRole('heading', { level: 1, name: 'Log in' })
    expect(router.state.location.pathname).toBe('/login')
    expect(screen.getByText(ALREADY_SET_UP)).toBeTruthy()
  })

  it('makes the owner wait after too many attempts', async () => {
    stubFetch(
      noAdmin(),
      problemResponse(
        {
          status: 429,
          code: 'too_many_requests',
          title: 'Too many attempts. Wait a moment and try again.',
          traceId: '00-d-01',
        },
        { 'Retry-After': '30' },
      ),
    )
    await renderAppAt('/setup')
    await screen.findByRole('heading', { name: 'Set up Mosaic' })

    fillValid()
    submit()

    const button = await screen.findByRole('button', {
      name: 'Try again in 30 seconds',
    })
    expect(button.hasAttribute('disabled')).toBe(true)
    expect(screen.getByRole('alert').textContent).toBe(
      'Too many attempts. Wait a moment and try again.',
    )
  })

  it('says so when the server stops answering', async () => {
    stubFetch(noAdmin(), emptyResponse(502))
    await renderAppAt('/setup')
    await screen.findByRole('heading', { name: 'Set up Mosaic' })

    fillValid()
    submit()

    expect((await screen.findByRole('alert')).textContent).toBe(UNAVAILABLE)
    expect(
      screen
        .getByRole('button', { name: 'Create account' })
        .hasAttribute('disabled'),
    ).toBe(false)
  })
})
