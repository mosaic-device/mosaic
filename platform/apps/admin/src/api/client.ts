import {
  ApiError,
  isAbortError,
  isApiErrorCode,
  type ApiErrorCode,
  type FieldErrors,
} from '@/api/errors'

/**
 * Same origin, always: the dev proxy and the server both answer `/api`, so
 * there is no base URL setting (Admin API Contract, section 1).
 */
export type ApiPath = `/api/${string}`

type RequestOptions = {
  method?: 'GET' | 'POST'
  body?: unknown
  signal?: AbortSignal
}

/**
 * Sends a request and returns the JSON body when it passes `check`.
 * Rejects with an `ApiError`, or with the browser's `AbortError` when
 * `signal` cancels the request.
 */
export async function requestJson<T>(
  path: ApiPath,
  options: RequestOptions & { check: (json: unknown) => json is T },
): Promise<T> {
  const { response, text } = await send(path, options)
  if (!response.ok) throw errorFrom(response, text)
  if (mediaType(response) !== 'application/json') {
    throw malformed(response, 'Expected a JSON body.')
  }
  const json = parseJson(text)
  if (json === undefined || !options.check(json)) {
    throw malformed(response, 'The JSON body has the wrong shape.')
  }
  return json
}

/** Like `requestJson`, for endpoints that answer `204 No Content`. */
export async function requestEmpty(
  path: ApiPath,
  options: RequestOptions = {},
): Promise<void> {
  const { response, text } = await send(path, options)
  if (!response.ok) throw errorFrom(response, text)
  if (response.status !== 204) {
    throw malformed(response, 'Expected 204 No Content.')
  }
}

async function send(
  path: ApiPath,
  { method = 'GET', body, signal }: RequestOptions,
): Promise<{ response: Response; text: string }> {
  const headers = new Headers({ Accept: 'application/json' })
  if (body !== undefined) headers.set('Content-Type', 'application/json')
  try {
    const response = await fetch(path, {
      method,
      headers,
      body: body === undefined ? undefined : JSON.stringify(body),
      credentials: 'same-origin',
      signal,
    })
    return { response, text: await response.text() }
  } catch (error) {
    if (isAbortError(error)) throw error
    throw new ApiError({ kind: 'unavailable', cause: error })
  }
}

/**
 * Only the Mosaic API answers with Problem Details. Any other error response
 * came from something in between (the Vite proxy, Tailscale Funnel, AirPlay
 * on port 5000), so the API itself is out of reach.
 */
function errorFrom(response: Response, text: string): ApiError {
  if (mediaType(response) !== 'application/problem+json') {
    return new ApiError({ kind: 'unavailable', status: response.status })
  }
  const problem = parseJson(text)
  if (!isProblem(problem)) {
    return malformed(response, 'The error body breaks the contract.')
  }
  return new ApiError({
    kind: 'problem',
    status: response.status,
    code: problem.code,
    title: problem.title,
    traceId: problem.traceId ?? undefined,
    fieldErrors: problem.errors,
    retryAfterSeconds: retryAfter(response),
  })
}

type Problem = {
  code: ApiErrorCode
  title: string
  traceId?: string | null
  errors?: FieldErrors
}

function isProblem(json: unknown): json is Problem {
  if (typeof json !== 'object' || json === null) return false
  const { status, code, title, traceId, errors } = json as Record<
    string,
    unknown
  >
  return (
    typeof status === 'number' &&
    isApiErrorCode(code) &&
    typeof title === 'string' &&
    (traceId === undefined ||
      traceId === null ||
      typeof traceId === 'string') &&
    (errors === undefined || isFieldErrors(errors))
  )
}

function isFieldErrors(value: unknown): value is FieldErrors {
  return (
    typeof value === 'object' &&
    value !== null &&
    !Array.isArray(value) &&
    Object.values(value).every(
      (messages) =>
        Array.isArray(messages) &&
        messages.every((message) => typeof message === 'string'),
    )
  )
}

/** Whole seconds only; an HTTP date or anything else is ignored. */
function retryAfter(response: Response): number | undefined {
  const value = response.headers.get('Retry-After')?.trim() ?? ''
  return /^\d+$/.test(value) ? Number(value) : undefined
}

function mediaType(response: Response): string {
  return (response.headers.get('Content-Type') ?? '')
    .split(';')[0]!
    .trim()
    .toLowerCase()
}

function parseJson(text: string): unknown {
  try {
    return JSON.parse(text) as unknown
  } catch {
    return undefined
  }
}

function malformed(response: Response, reason: string): ApiError {
  return new ApiError({
    kind: 'malformed',
    status: response.status,
    cause: new Error(reason),
  })
}
