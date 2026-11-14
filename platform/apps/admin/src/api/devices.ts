import { requestEmpty, requestJson } from '@/api/client'
import type { FieldMessages } from '@/api/credential-rules'

// The device endpoints in the Admin API Contract, section 5.

/**
 * Every device the API returns is paired. `connected` says whether it is
 * online right now; `pairedAt` is the server's ISO 8601 UTC timestamp.
 */
export type Device = { id: number; pairedAt: string; connected: boolean }
export type DeviceResult = { device: Device }
export type DeviceList = { devices: Device[] }
export type ClaimInput = { code: string }

/** The letters and digits a pairing code uses: no 0, 1, I or O. */
const CODE_ALPHABET = /^[ABCDEFGHJKLMNPQRSTUVWXYZ23456789]{8}$/

/**
 * Mirrors the server's code check (`PairingCodes.TryNormalize`) and message:
 * spaces trimmed from both ends, dashes removed, any case. The server stays
 * the authority, so the code is sent as typed.
 */
export function validateClaim(input: ClaimInput): FieldMessages<'code'> {
  const code = input.code.replace(/^ +| +$/g, '').replaceAll('-', '')
  return CODE_ALPHABET.test(code.toUpperCase())
    ? {}
    : { code: 'Enter the 8-character code shown on the device.' }
}

/**
 * Pairs the device showing this code. Claiming the same code again returns
 * the same device, so retrying after a lost answer is safe.
 */
export function claimDevice(
  input: ClaimInput,
  signal?: AbortSignal,
): Promise<DeviceResult> {
  return requestJson('/api/devices/claim', {
    method: 'POST',
    body: input,
    signal,
    check: isDeviceResult,
  })
}

/** Every paired device, oldest pairing first. Removed devices never appear. */
export function listDevices(signal?: AbortSignal): Promise<DeviceList> {
  return requestJson('/api/devices', { signal, check: isDeviceList })
}

/**
 * Removes the device at once, even while it's offline. A `device_not_found`
 * answer means it is already gone, for example after a retry.
 */
export function removeDevice(id: number, signal?: AbortSignal): Promise<void> {
  return requestEmpty(`/api/devices/${id}`, { method: 'DELETE', signal })
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null
}

function isDevice(value: unknown): value is Device {
  return (
    isRecord(value) &&
    Number.isInteger(value.id) &&
    typeof value.pairedAt === 'string' &&
    !Number.isNaN(Date.parse(value.pairedAt)) &&
    typeof value.connected === 'boolean'
  )
}

function isDeviceResult(json: unknown): json is DeviceResult {
  return isRecord(json) && isDevice(json.device)
}

function isDeviceList(json: unknown): json is DeviceList {
  return (
    isRecord(json) &&
    Array.isArray(json.devices) &&
    json.devices.every(isDevice)
  )
}
