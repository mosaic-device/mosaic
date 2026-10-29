import { isApiError } from '@/api/errors'

const UNAVAILABLE =
  "Can't reach the Mosaic server. Check that it's running, then try again."
const MALFORMED = "The server sent a response the app didn't expect. Try again."
const BUG = 'Something went wrong on this page. Try again, or reload the page.'

/**
 * One plain message for any error a page or form might show. Problems use
 * the server's `title`, which the contract makes safe to show. Anything that
 * is not an `ApiError` is a bug in the app, so it does not blame the server.
 */
export function describeError(error: unknown): {
  message: string
  reference?: string
} {
  if (!isApiError(error)) return { message: BUG }
  switch (error.kind) {
    case 'problem':
      return error.code === 'internal_error' && error.traceId
        ? { message: error.title ?? MALFORMED, reference: error.traceId }
        : { message: error.title ?? MALFORMED }
    case 'unavailable':
      return { message: UNAVAILABLE }
    case 'malformed':
      return { message: MALFORMED }
  }
}
