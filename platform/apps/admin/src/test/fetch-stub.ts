import { vi } from 'vitest'

type FetchCall = { url: string; init: RequestInit }

type StubAnswer =
  Response | Error | ((call: FetchCall) => Response | Promise<Response>)

/**
 * Replaces `fetch` with a stub that answers calls in order. An `Error` answer
 * rejects, like a network failure. Like the real `fetch`, an aborted signal
 * rejects with the signal's reason, even while an answer is still pending.
 */
export function stubFetch(...answers: StubAnswer[]): { calls: FetchCall[] } {
  const calls: FetchCall[] = []
  vi.stubGlobal(
    'fetch',
    async (input: RequestInfo | URL, init: RequestInit = {}) => {
      const call = { url: String(input), init }
      calls.push(call)
      const answer = answers.shift()
      if (answer === undefined) {
        throw new Error(`Unexpected fetch: ${call.url}`)
      }
      const signal = init.signal
      if (signal?.aborted) throw signal.reason
      // jsdom's DOMException is not `instanceof Error`, so check the other two.
      const response =
        answer instanceof Response
          ? Promise.resolve(answer)
          : typeof answer === 'function'
            ? Promise.resolve(call).then(answer)
            : Promise.reject(answer)
      if (!signal) return response
      return Promise.race([
        response,
        new Promise<never>((_, reject) => {
          signal.addEventListener('abort', () => reject(signal.reason), {
            once: true,
          })
        }),
      ])
    },
  )
  return { calls }
}

export function jsonResponse(body: unknown, status = 200): Response {
  return new Response(JSON.stringify(body), {
    status,
    headers: { 'Content-Type': 'application/json; charset=utf-8' },
  })
}

/** A Problem Details error; the HTTP status comes from `body.status`. */
export function problemResponse(
  body: Record<string, unknown>,
  headers: Record<string, string> = {},
): Response {
  return new Response(JSON.stringify(body), {
    status: Number(body.status),
    headers: { 'Content-Type': 'application/problem+json', ...headers },
  })
}

export function emptyResponse(
  status: number,
  headers: Record<string, string> = {},
): Response {
  return new Response(null, { status, headers })
}

export function textResponse(
  text: string,
  status: number,
  contentType: string,
): Response {
  return new Response(text, {
    status,
    headers: { 'Content-Type': contentType },
  })
}
