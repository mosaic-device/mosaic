import { describe, expect, it } from 'vitest'
import {
  completeSetup,
  getSession,
  getSetupStatus,
  login,
  logout,
} from '@/api/auth'
import { ApiError } from '@/api/errors'
import {
  emptyResponse,
  jsonResponse,
  problemResponse,
  stubFetch,
} from '@/test/fetch-stub'

// Examples from the Admin API Contract, sections 2 and 3.
const setupInput = {
  username: 'bridger',
  password: 'mosaic-desk-2026',
  setupCode: 'K7QF-2M9X',
}
const loginInput = { username: 'bridger', password: 'mosaic-desk-2026' }
const adminResult = { admin: { username: 'bridger' } }
const session = {
  admin: { username: 'bridger' },
  expiresAt: '2026-10-26T18:00:00Z',
}

async function apiFailure(promise: Promise<unknown>): Promise<ApiError> {
  const error: unknown = await promise.then(
    () => {
      throw new Error('Expected the request to fail')
    },
    (failure: unknown) => failure,
  )
  expect(error).toBeInstanceOf(ApiError)
  return error as ApiError
}

describe('getSetupStatus', () => {
  it('asks the server whether setup is needed', async () => {
    const { calls } = stubFetch(jsonResponse({ setupRequired: true }))
    await expect(getSetupStatus()).resolves.toEqual({ setupRequired: true })
    expect(calls[0]?.url).toBe('/api/setup')
    expect(calls[0]?.init.method).toBe('GET')
  })

  it('rejects a status that is not a boolean', async () => {
    stubFetch(jsonResponse({ setupRequired: 'yes' }))
    expect((await apiFailure(getSetupStatus())).kind).toBe('malformed')
  })
})

describe('completeSetup', () => {
  it('creates the admin', async () => {
    const { calls } = stubFetch(jsonResponse(adminResult, 201))
    await expect(completeSetup(setupInput)).resolves.toEqual(adminResult)
    expect(calls[0]?.url).toBe('/api/setup')
    expect(calls[0]?.init.method).toBe('POST')
    expect(JSON.parse(String(calls[0]?.init.body))).toEqual(setupInput)
  })

  it('reports a wrong setup code and a finished setup', async () => {
    stubFetch(
      problemResponse({
        status: 403,
        code: 'invalid_setup_code',
        title: 'The setup code is incorrect.',
      }),
      problemResponse({
        status: 409,
        code: 'setup_already_completed',
        title: 'Setup has already been completed.',
      }),
    )
    expect((await apiFailure(completeSetup(setupInput))).code).toBe(
      'invalid_setup_code',
    )
    expect((await apiFailure(completeSetup(setupInput))).code).toBe(
      'setup_already_completed',
    )
  })
})

describe('login', () => {
  it('logs the admin in', async () => {
    const { calls } = stubFetch(jsonResponse(adminResult))
    await expect(login(loginInput)).resolves.toEqual(adminResult)
    expect(calls[0]?.url).toBe('/api/auth/login')
    expect(calls[0]?.init.method).toBe('POST')
    expect(JSON.parse(String(calls[0]?.init.body))).toEqual(loginInput)
  })

  it('reports that setup comes first', async () => {
    stubFetch(
      problemResponse({
        status: 409,
        code: 'setup_required',
        title: 'Create the admin account before logging in.',
      }),
    )
    expect((await apiFailure(login(loginInput))).code).toBe('setup_required')
  })

  it('rejects an admin without a username', async () => {
    stubFetch(jsonResponse({ admin: {} }))
    expect((await apiFailure(login(loginInput))).kind).toBe('malformed')
  })
})

describe('logout', () => {
  it('ends the session without a body', async () => {
    const { calls } = stubFetch(emptyResponse(204))
    await expect(logout()).resolves.toBeUndefined()
    expect(calls[0]?.url).toBe('/api/auth/logout')
    expect(calls[0]?.init.method).toBe('POST')
    expect(calls[0]?.init.body).toBeUndefined()
  })
})

describe('getSession', () => {
  it('returns the admin and expiry', async () => {
    const { calls } = stubFetch(jsonResponse(session))
    await expect(getSession()).resolves.toEqual(session)
    expect(calls[0]?.url).toBe('/api/auth/session')
    expect(calls[0]?.init.method).toBe('GET')
  })

  it('rejects an expiry that is not a date', async () => {
    stubFetch(jsonResponse({ ...session, expiresAt: 'tomorrow' }))
    expect((await apiFailure(getSession())).kind).toBe('malformed')
  })

  it('reports a missing or expired session', async () => {
    stubFetch(
      problemResponse({
        status: 401,
        code: 'unauthenticated',
        title: 'Log in to continue.',
      }),
    )
    expect((await apiFailure(getSession())).code).toBe('unauthenticated')
  })
})

describe('cancellation', () => {
  it('passes the signal to every call', async () => {
    const { calls } = stubFetch(
      jsonResponse({ setupRequired: false }),
      jsonResponse(adminResult, 201),
      jsonResponse(adminResult),
      emptyResponse(204),
      jsonResponse(session),
    )
    const { signal } = new AbortController()
    await getSetupStatus(signal)
    await completeSetup(setupInput, signal)
    await login(loginInput, signal)
    await logout(signal)
    await getSession(signal)
    expect(calls.map((call) => call.init.signal)).toEqual(Array(5).fill(signal))
  })
})
