import { LoaderCircle } from 'lucide-react'

/**
 * Setup and login's loading state: only a spinner, so no card appears and
 * then gives way to an error when the server can't be reached.
 */
function PublicPageLoading() {
  return (
    <div
      role="status"
      className="text-muted-foreground grid place-items-center"
    >
      <LoaderCircle aria-hidden="true" className="size-8 animate-spin" />
      <span className="sr-only">Loading…</span>
    </div>
  )
}

export { PublicPageLoading }
