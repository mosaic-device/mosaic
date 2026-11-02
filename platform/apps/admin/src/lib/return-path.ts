// Any base works: only the origin is compared, and it never reaches the page.
const BASE = 'http://mosaic.invalid'
const NO_RETURN = new Set(['/', '/login', '/setup'])

/**
 * The page to open after login, or `undefined` for Home. Only a path inside
 * this app is kept, so a crafted `?redirect=` link can't send the owner to
 * another site after they log in.
 */
export function safeReturnPath(value: unknown): string | undefined {
  if (typeof value !== 'string' || !value.startsWith('/')) return undefined
  let url: URL
  try {
    url = new URL(value, BASE)
  } catch {
    return undefined
  }
  if (url.origin !== BASE || NO_RETURN.has(url.pathname)) return undefined
  return url.pathname + url.search + url.hash
}

/** The return path for a router location's `href` (path, search and hash). */
export function returnPathFor(href: string): string | undefined {
  return safeReturnPath(href)
}
