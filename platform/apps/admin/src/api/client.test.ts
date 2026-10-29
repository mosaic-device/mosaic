import { describe, expect, it } from 'vitest'
import { requestEmpty, requestJson } from '@/api/client'
import { ApiError, isAbortError, isApiError } from '@/api/errors'
import {
  emptyResponse,
  jsonResponse,
  problemResponse,
  stubFetch,
  textResponse,
} from '@/test/fetch-stub'

type Ok = { ok: true }

function isOk(json: unknown): json is Ok {
  return (
    typeof json === 'object' &&
    json !== null &&
    (json as { ok?: unknown }).ok === true
  )
}

async function failure(promise: Promise<unknown>): Promise<unknown> {
  try {
    await promise
  } catch (error) {
    return error
  }
  throw new Error('Expected the request to fail')
}

async function apiFailure(promise: Promise<unknown>): Promise<ApiError> {
  const error = await failure(promise)
  expect(error).toBeInstanceOf(ApiError)
  return error as ApiError
}

function headersOf(init: RequestInit): Headers {
  return new Headers(init.headers)
}

describe('request details', () => {
  it('sends a relative same-origin JSON request', async () => {
    const { calls } = stubFetch(jsonResponse({ ok: true }))
    const controller = new AbortController()
    await requestJson('/api/x', {
      method: 'POST',
      body: { a: 1 },
      signal: controller.signal,
      check: isOk,
    })
    const [call] = calls
    expect(call?.url).toBe('/api/x')
    expect(call?.init.method).toBe('POST')
    expect(call?.init.credentials).toBe('same-origin')
    expect(headersOf(call!.init).get('Accept')).toBe('application/json')
    expect(headersOf(call!.init).get('Content-Type')).toBe('application/json')
    expect(call?.init.body).toBe('{"a":1}')
    expect(call?.init.signal).toBe(controller.signal)
  })

  it('sends no content type without a body', async () => {
    const { calls } = stubFetch(emptyResponse(204), jsonResponse({ ok: true }))
    await requestEmpty('/api/x', { method: 'POST' })
    await requestJson('/api/y', { check: isOk })
    expect(headersOf(calls[0]!.init).has('Content-Type')).toBe(false)
    expect(calls[0]?.init.body).toBeUndefined()
    expect(calls[1]?.init.method).toBe('GET')
    expect(headersOf(calls[1]!.init).has('Content-Type')).toBe(false)
  })
})

describe('success', () => {
  it('returns data that passes the check', async () => {
    stubFetch(
      jsonResponse({ ok: true }),
      textResponse('{"ok":true}', 200, 'application/json'),
    )
    await expect(requestJson('/api/x', { check: isOk })).resolves.toEqual({
      ok: true,
    })
    await expect(requestJson('/api/x', { check: isOk })).resolves.toEqual({
      ok: true,
    })
  })

  it('returns nothing for 204', async () => {
    stubFetch(emptyResponse(204))
    await expect(
      requestEmpty('/api/x', { method: 'POST' }),
    ).resolves.toBeUndefined()
  })
})

describe('problem responses', () => {
  it('keeps field errors from a validation failure', async () => {
    stubFetch(
      problemResponse({
        status: 400,
        code: 'validation_failed',
        title: 'Some fields need attention.',
        traceId: '00-abc-01',
        errors: { password: ['Must be at least 12 characters.'] },
      }),
    )
    const error = await apiFailure(requestJson('/api/x', { check: isOk }))
    expect(error.kind).toBe('problem')
    expect(error.status).toBe(400)
    expect(error.code).toBe('validation_failed')
    expect(error.title).toBe('Some fields need attention.')
    expect(error.message).toBe('Some fields need attention.')
    expect(error.fieldErrors.password?.[0]).toBe(
      'Must be at least 12 characters.',
    )
  })

  it('tells an expired session apart from a wrong password', async () => {
    stubFetch(
      problemResponse({
        status: 401,
        code: 'unauthenticated',
        title: 'Log in to continue.',
      }),
      problemResponse({
        status: 401,
        code: 'invalid_credentials',
        title: 'Incorrect username or password.',
      }),
    )
    const expired = await apiFailure(requestJson('/api/x', { check: isOk }))
    const wrong = await apiFailure(requestJson('/api/x', { check: isOk }))
    expect(expired.code).toBe('unauthenticated')
    expect(expired.title).toBe('Log in to continue.')
    expect(expired.fieldErrors).toEqual({})
    expect(wrong.code).toBe('invalid_credentials')
    expect(wrong.title).toBe('Incorrect username or password.')
  })

  it('reads Retry-After seconds from a 429', async () => {
    const tooMany = {
      status: 429,
      code: 'too_many_requests',
      title: 'Too many attempts. Wait a moment and try again.',
    }
    stubFetch(
      problemResponse(tooMany, { 'Retry-After': '7' }),
      problemResponse(tooMany, {
        'Retry-After': 'Wed, 21 Oct 2026 07:28:00 GMT',
      }),
      problemResponse(tooMany, { 'Retry-After': '' }),
      problemResponse(tooMany, { 'Retry-After': 'soon' }),
    )
    const seconds = await apiFailure(requestJson('/api/x', { check: isOk }))
    expect(seconds.code).toBe('too_many_requests')
    expect(seconds.retryAfterSeconds).toBe(7)
    for (let i = 0; i < 3; i++) {
      const other = await apiFailure(requestJson('/api/x', { check: isOk }))
      expect(other.retryAfterSeconds).toBeUndefined()
    }
  })

  it('keeps the trace ID of an internal error', async () => {
    const internal = {
      status: 500,
      code: 'internal_error',
      title: 'Something went wrong.',
    }
    stubFetch(
      problemResponse({ ...internal, traceId: '00-abc-01' }),
      problemResponse({ ...internal, traceId: null }),
      problemResponse(internal),
    )
    const withId = await apiFailure(requestJson('/api/x', { check: isOk }))
    expect(withId.kind).toBe('problem')
    expect(withId.code).toBe('internal_error')
    expect(withId.traceId).toBe('00-abc-01')
    for (let i = 0; i < 2; i++) {
      const without = await apiFailure(requestJson('/api/x', { check: isOk }))
      expect(without.kind).toBe('problem')
      expect(without.traceId).toBeUndefined()
    }
  })

  it('reads Problem Details sent with a charset', async () => {
    stubFetch(
      textResponse(
        JSON.stringify({
          status: 409,
          code: 'setup_required',
          title: 'Create the admin account before logging in.',
        }),
        409,
        'application/problem+json; charset=utf-8',
      ),
    )
    const error = await apiFailure(requestJson('/api/x', { check: isOk }))
    expect(error.kind).toBe('problem')
    expect(error.code).toBe('setup_required')
  })
})

describe('unavailable server', () => {
  it('reports a failed fetch', async () => {
    stubFetch(new TypeError('Failed to fetch'))
    const error = await apiFailure(requestJson('/api/x', { check: isOk }))
    expect(error.kind).toBe('unavailable')
    expect(error.status).toBeUndefined()
  })

  it('reports error responses that are not Problem Details', async () => {
    stubFetch(
      emptyResponse(502),
      emptyResponse(403, { Server: 'AirTunes/860.7.1' }),
      emptyResponse(413),
      emptyResponse(404),
      textResponse('<html>Bad gateway</html>', 502, 'text/html'),
      jsonResponse({ error: 'x' }, 500),
    )
    for (const status of [502, 403, 413, 404, 502, 500]) {
      const error = await apiFailure(requestJson('/api/x', { check: isOk }))
      expect(error.kind).toBe('unavailable')
      expect(error.status).toBe(status)
    }
  })
})

describe('malformed responses', () => {
  it('rejects success bodies the app cannot use', async () => {
    stubFetch(
      textResponse('<!doctype html>', 200, 'text/html'),
      jsonResponse({ ok: false }),
      textResponse('', 200, 'application/json'),
      textResponse('{', 200, 'application/json'),
    )
    for (let i = 0; i < 4; i++) {
      const error = await apiFailure(requestJson('/api/x', { check: isOk }))
      expect(error.kind).toBe('malformed')
    }
  })

  it('rejects a body where no content is expected', async () => {
    stubFetch(jsonResponse({}))
    const error = await apiFailure(requestEmpty('/api/x', { method: 'POST' }))
    expect(error.kind).toBe('malformed')
  })

  it('rejects Problem Details that break the contract', async () => {
    stubFetch(
      textResponse('{', 400, 'application/problem+json'),
      problemResponse({ status: 409, code: 'brand_new', title: 'x' }),
      problemResponse({ status: 400, code: 'validation_failed' }),
      problemResponse({
        status: 400,
        code: 'validation_failed',
        title: 'Some fields need attention.',
        errors: { password: 'not an array' },
      }),
    )
    for (let i = 0; i < 4; i++) {
      const error = await apiFailure(requestJson('/api/x', { check: isOk }))
      expect(error.kind).toBe('malformed')
    }
  })
})

describe('cancellation', () => {
  it('rethrows aborts untouched', async () => {
    stubFetch(
      jsonResponse({ ok: true }),
      new DOMException('The operation was aborted.', 'AbortError'),
    )
    const controller = new AbortController()
    controller.abort()
    const before = await failure(
      requestJson('/api/x', { signal: controller.signal, check: isOk }),
    )
    expect(isAbortError(before)).toBe(true)
    expect(isApiError(before)).toBe(false)
    const during = await failure(requestJson('/api/x', { check: isOk }))
    expect(isAbortError(during)).toBe(true)
    expect(isApiError(during)).toBe(false)
  })
})
