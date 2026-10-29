/** The `code` values in the Admin API Contract, section 3. */
export const API_ERROR_CODES = [
  'validation_failed',
  'invalid_credentials',
  'unauthenticated',
  'invalid_setup_code',
  'forbidden_origin',
  'setup_already_completed',
  'setup_required',
  'too_many_requests',
  'internal_error',
] as const

export type ApiErrorCode = (typeof API_ERROR_CODES)[number]

export function isApiErrorCode(value: unknown): value is ApiErrorCode {
  return (API_ERROR_CODES as readonly unknown[]).includes(value)
}

/**
 * - `problem`: the server answered with one of the contract's error codes.
 * - `unavailable`: the server could not be reached, or something other than
 *   the Mosaic API answered (a proxy, a gateway, another app on the port).
 * - `malformed`: the server's answer does not match the contract.
 */
export type ApiErrorKind = 'problem' | 'unavailable' | 'malformed'

/** Field name to messages, from a `validation_failed` response. */
export type FieldErrors = Readonly<Record<string, readonly string[]>>

export type ApiErrorInit =
  | {
      kind: 'problem'
      status: number
      code: ApiErrorCode
      title: string
      traceId?: string
      fieldErrors?: FieldErrors
      retryAfterSeconds?: number
    }
  | { kind: 'unavailable' | 'malformed'; status?: number; cause?: unknown }

export class ApiError extends Error {
  override readonly name = 'ApiError'
  readonly kind: ApiErrorKind
  readonly status: number | undefined
  /** Set for `problem` only. Branch on this, never on `status` or `title`. */
  readonly code: ApiErrorCode | undefined
  /** Set for `problem` only. Safe to show as-is. */
  readonly title: string | undefined
  readonly traceId: string | undefined
  readonly fieldErrors: FieldErrors
  readonly retryAfterSeconds: number | undefined

  constructor(init: ApiErrorInit) {
    if (init.kind === 'problem') {
      super(init.title)
      this.code = init.code
      this.title = init.title
      this.traceId = init.traceId
      this.fieldErrors = init.fieldErrors ?? {}
      this.retryAfterSeconds = init.retryAfterSeconds
    } else {
      super(init.kind, { cause: init.cause })
      this.code = undefined
      this.title = undefined
      this.traceId = undefined
      this.fieldErrors = {}
      this.retryAfterSeconds = undefined
    }
    this.kind = init.kind
    this.status = init.status
  }
}

export function isApiError(error: unknown): error is ApiError {
  return error instanceof ApiError
}

/** True for a cancelled request. Not an error to show; UI ignores it. */
export function isAbortError(error: unknown): boolean {
  return (
    typeof error === 'object' &&
    error !== null &&
    (error as { name?: unknown }).name === 'AbortError'
  )
}
