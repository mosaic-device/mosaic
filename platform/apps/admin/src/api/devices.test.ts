import { describe, expect, it } from 'vitest'
import {
  claimDevice,
  listDevices,
  removeDevice,
  sendTestNotification,
  validateClaim,
} from '@/api/devices'
import { ApiError } from '@/api/errors'
import {
  emptyResponse,
  jsonResponse,
  problemResponse,
  stubFetch,
} from '@/test/fetch-stub'

// Examples from the Admin API Contract, section 5.
const device = { id: 3, pairedAt: '2026-11-06T18:30:00Z', connected: true }
const CODE_MESSAGE = 'Enter the 8-character code shown on the device.'

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

describe('validateClaim', () => {
  it.each([
    'K7QF-2M9X',
    'K7QF2M9X',
    'k7qf-2m9x',
    'k7qf2m9x',
    '  K7QF-2M9X  ',
    'K7-QF-2M-9X',
  ])('accepts %j', (code) => {
    expect(validateClaim({ code })).toEqual({})
  })

  it.each([
    '',
    '   ',
    'K7QF-2M9',
    'K7QF-2M9XA',
    'K7QF 2M9X',
    'K0QF-2M9X',
    'K1QF-2M9X',
    'KIQF-2M9X',
    'KOQF-2M9X',
    'K7QF_2M9X',
  ])('rejects %j with the server message', (code) => {
    expect(validateClaim({ code })).toEqual({ code: CODE_MESSAGE })
  })
})

describe('claimDevice', () => {
  it('claims the code as typed and returns the device', async () => {
    const { calls } = stubFetch(jsonResponse({ device }))
    await expect(claimDevice({ code: 'k7qf-2m9x' })).resolves.toEqual({
      device,
    })
    expect(calls[0]?.url).toBe('/api/devices/claim')
    expect(calls[0]?.init.method).toBe('POST')
    expect(JSON.parse(String(calls[0]?.init.body))).toEqual({
      code: 'k7qf-2m9x',
    })
  })

  it('reports an expired code', async () => {
    stubFetch(
      problemResponse({
        status: 410,
        code: 'pairing_code_expired',
        title: 'That code has expired; enter the one the device shows now.',
      }),
    )
    const error = await apiFailure(claimDevice({ code: 'K7QF-2M9X' }))
    expect(error.code).toBe('pairing_code_expired')
  })

  it.each([
    { device: { ...device, id: '3' } },
    { device: { ...device, id: 3.5 } },
    { device: { ...device, pairedAt: 'yesterday' } },
    { device: { id: 3, pairedAt: device.pairedAt } },
    { devices: [device] },
  ])('rejects a device the app cannot use: %j', async (body) => {
    stubFetch(jsonResponse(body))
    const error = await apiFailure(claimDevice({ code: 'K7QF-2M9X' }))
    expect(error.kind).toBe('malformed')
  })
})

describe('listDevices', () => {
  it('returns the paired devices', async () => {
    const offline = {
      id: 5,
      pairedAt: '2026-11-14T20:12:00Z',
      connected: false,
    }
    const { calls } = stubFetch(jsonResponse({ devices: [device, offline] }))
    await expect(listDevices()).resolves.toEqual({
      devices: [device, offline],
    })
    expect(calls[0]?.url).toBe('/api/devices')
    expect(calls[0]?.init.method).toBe('GET')
  })

  it('returns an empty list', async () => {
    stubFetch(jsonResponse({ devices: [] }))
    await expect(listDevices()).resolves.toEqual({ devices: [] })
  })

  it.each([
    {},
    { devices: null },
    { devices: device },
    { devices: [device, { ...device, connected: 'yes' }] },
    { device },
  ])('rejects a list the app cannot use: %j', async (body) => {
    stubFetch(jsonResponse(body))
    const error = await apiFailure(listDevices())
    expect(error.kind).toBe('malformed')
  })
})

describe('removeDevice', () => {
  it('deletes the device by ID', async () => {
    const { calls } = stubFetch(emptyResponse(204))
    await expect(removeDevice(3)).resolves.toBeUndefined()
    expect(calls[0]?.url).toBe('/api/devices/3')
    expect(calls[0]?.init.method).toBe('DELETE')
    expect(calls[0]?.init.body).toBeUndefined()
  })

  it('reports a device that is already gone', async () => {
    stubFetch(
      problemResponse({
        status: 404,
        code: 'device_not_found',
        title: "That device isn't paired with this server.",
      }),
    )
    const error = await apiFailure(removeDevice(3))
    expect(error.kind).toBe('problem')
    expect(error.code).toBe('device_not_found')
  })
})

describe('sendTestNotification', () => {
  it.each(['delivered', 'offline', 'no_answer', 'rejected'])(
    'posts to the device and returns the outcome %j',
    async (outcome) => {
      const { calls } = stubFetch(jsonResponse({ outcome }))
      await expect(sendTestNotification(3)).resolves.toEqual({ outcome })
      expect(calls[0]?.url).toBe('/api/devices/3/test-notification')
      expect(calls[0]?.init.method).toBe('POST')
      expect(calls[0]?.init.body).toBeUndefined()
    },
  )

  it.each([
    {},
    { outcome: 'lost' },
    { outcome: null },
    { result: 'delivered' },
  ])('rejects an answer the app cannot use: %j', async (body) => {
    stubFetch(jsonResponse(body))
    const error = await apiFailure(sendTestNotification(3))
    expect(error.kind).toBe('malformed')
  })

  it('reports a device that is no longer paired', async () => {
    stubFetch(
      problemResponse({
        status: 404,
        code: 'device_not_found',
        title: "That device isn't paired with this server.",
      }),
    )
    const error = await apiFailure(sendTestNotification(3))
    expect(error.kind).toBe('problem')
    expect(error.code).toBe('device_not_found')
  })

  it('reports another test still waiting, with how long to wait', async () => {
    stubFetch(
      problemResponse(
        {
          status: 429,
          code: 'too_many_requests',
          title: 'Too many attempts. Wait a moment and try again.',
        },
        { 'Retry-After': '7' },
      ),
    )
    const error = await apiFailure(sendTestNotification(3))
    expect(error.code).toBe('too_many_requests')
    expect(error.retryAfterSeconds).toBe(7)
  })
})
