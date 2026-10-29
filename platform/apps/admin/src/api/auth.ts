import { requestEmpty, requestJson } from '@/api/client'

// The five endpoints in the Admin API Contract, section 2. Field rules are
// the server's to enforce; inputs are sent as given.

export type Admin = { username: string }
export type AdminResult = { admin: Admin }
export type SetupStatus = { setupRequired: boolean }
/** `expiresAt` is the server's ISO 8601 UTC timestamp. */
export type Session = { admin: Admin; expiresAt: string }

export type SetupInput = {
  username: string
  password: string
  setupCode: string
}
export type LoginInput = { username: string; password: string }

/** Whether an admin exists yet (first-run detection). */
export function getSetupStatus(signal?: AbortSignal): Promise<SetupStatus> {
  return requestJson('/api/setup', { signal, check: isSetupStatus })
}

/** Creates the one admin with the server's setup code and logs them in. */
export function completeSetup(
  input: SetupInput,
  signal?: AbortSignal,
): Promise<AdminResult> {
  return requestJson('/api/setup', {
    method: 'POST',
    body: input,
    signal,
    check: isAdminResult,
  })
}

export function login(
  input: LoginInput,
  signal?: AbortSignal,
): Promise<AdminResult> {
  return requestJson('/api/auth/login', {
    method: 'POST',
    body: input,
    signal,
    check: isAdminResult,
  })
}

/** Ends the session. The server answers 204 whether or not one existed. */
export function logout(signal?: AbortSignal): Promise<void> {
  return requestEmpty('/api/auth/logout', { method: 'POST', signal })
}

/** The signed-in admin, or an `unauthenticated` error. */
export function getSession(signal?: AbortSignal): Promise<Session> {
  return requestJson('/api/auth/session', { signal, check: isSession })
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null
}

function isAdmin(value: unknown): value is Admin {
  return isRecord(value) && typeof value.username === 'string'
}

function isSetupStatus(json: unknown): json is SetupStatus {
  return isRecord(json) && typeof json.setupRequired === 'boolean'
}

function isAdminResult(json: unknown): json is AdminResult {
  return isRecord(json) && isAdmin(json.admin)
}

function isSession(json: unknown): json is Session {
  return (
    isRecord(json) &&
    isAdmin(json.admin) &&
    typeof json.expiresAt === 'string' &&
    !Number.isNaN(Date.parse(json.expiresAt))
  )
}
