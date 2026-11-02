import { fireEvent, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import {
  jsonResponse,
  problemResponse,
  sessionResponse,
  stubFetch,
} from '@/test/fetch-stub'
import { renderAppAt } from '@/test/render-app'

const noAdmin = () => jsonResponse({ setupRequired: true })
const adminExists = () => jsonResponse({ setupRequired: false })

function fill(label: string, value: string) {
  fireEvent.change(screen.getByLabelText(label), { target: { value } })
}

function submit() {
  fireEvent.click(screen.getByRole('button', { name: 'Log in' }))
}

async function openLogin(...answers: Parameters<typeof stubFetch>) {
  const stub = stubFetch(adminExists(), ...answers)
  const router = await renderAppAt('/login')
  await screen.findByRole('heading', { level: 1, name: 'Log in' })
  return { ...stub, router }
}

describe('login page', () => {
  it('shows the login form once an admin exists', async () => {
    await openLogin()

    expect(document.title).toBe('Log in · Mosaic Admin')
    expect(screen.getByLabelText('Password').getAttribute('autocomplete')).toBe(
      'current-password',
    )
    expect(screen.queryByText(/already set up/)).toBeNull()
  })

  it('sends a visitor to setup while no admin exists', async () => {
    stubFetch(noAdmin(), noAdmin())
    const router = await renderAppAt('/login')

    await screen.findByRole('heading', { level: 1, name: 'Set up Mosaic' })
    expect(router.state.location.pathname).toBe('/setup')
  })

  it('only checks that both fields are filled in', async () => {
    const { calls } = await openLogin()

    submit()

    expect(screen.getAllByText('Required.')).toHaveLength(2)
    expect(document.activeElement).toBe(screen.getByLabelText('Username'))
    expect(calls).toHaveLength(1)
  })

  it('logs in and opens Home', async () => {
    const { calls, router } = await openLogin(
      jsonResponse({ admin: { username: 'bridger' } }),
      sessionResponse(),
    )

    fill('Username', 'Bridger ')
    fill('Password', 'short')
    submit()

    await screen.findByRole('heading', { level: 1, name: 'Welcome' })
    expect(router.state.location.pathname).toBe('/')
    expect(calls[1]?.url).toBe('/api/auth/login')
    expect(JSON.parse(String(calls[1]?.init.body))).toEqual({
      username: 'Bridger',
      password: 'short',
    })
  })

  it('sends values a password manager filled in without an input event', async () => {
    const { calls } = await openLogin(
      jsonResponse({ admin: { username: 'bridger' } }),
      sessionResponse(),
    )

    screen.getByLabelText<HTMLInputElement>('Username').value = 'bridger'
    screen.getByLabelText<HTMLInputElement>('Password').value = 'saved-password'
    submit()

    await screen.findByRole('heading', { level: 1, name: 'Welcome' })
    expect(JSON.parse(String(calls[1]?.init.body))).toEqual({
      username: 'bridger',
      password: 'saved-password',
    })
  })

  it('turns off autocorrect on the username', async () => {
    await openLogin()
    expect(screen.getByLabelText('Username').getAttribute('autocorrect')).toBe(
      'off',
    )
  })

  it('clears the password after a wrong login and keeps the username', async () => {
    await openLogin(
      problemResponse({
        status: 401,
        code: 'invalid_credentials',
        title: 'Incorrect username or password.',
        traceId: '00-a-01',
      }),
    )

    fill('Username', 'bridger')
    fill('Password', 'not-the-password')
    submit()

    const alert = await screen.findByRole('alert')
    expect(alert.textContent).toBe('Incorrect username or password.')
    const password = screen.getByLabelText<HTMLInputElement>('Password')
    expect(password.value).toBe('')
    expect(document.activeElement).toBe(password)
    expect(screen.getByLabelText<HTMLInputElement>('Username').value).toBe(
      'bridger',
    )
  })

  it('moves to setup when the server has no admin', async () => {
    const { router } = await openLogin(
      problemResponse({
        status: 409,
        code: 'setup_required',
        title: 'Create the admin account before logging in.',
        traceId: '00-b-01',
      }),
      noAdmin(),
    )

    fill('Username', 'bridger')
    fill('Password', 'a-long-password')
    submit()

    await screen.findByRole('heading', { level: 1, name: 'Set up Mosaic' })
    expect(router.state.location.pathname).toBe('/setup')
  })

  it('keeps the button disabled while the server answers', async () => {
    await openLogin(() => new Promise<Response>(() => {}))

    fill('Username', 'bridger')
    fill('Password', 'a-long-password')
    submit()

    const button = await screen.findByRole('button', { name: 'Logging in…' })
    expect(button.hasAttribute('disabled')).toBe(true)
  })

  it('makes the owner wait after too many attempts', async () => {
    await openLogin(
      problemResponse(
        {
          status: 429,
          code: 'too_many_requests',
          title: 'Too many attempts. Wait a moment and try again.',
          traceId: '00-c-01',
        },
        { 'Retry-After': '1' },
      ),
    )

    fill('Username', 'bridger')
    fill('Password', 'a-long-password')
    submit()

    const button = await screen.findByRole('button', {
      name: 'Try again in 1 second',
    })
    expect(button.hasAttribute('disabled')).toBe(true)
  })
})
