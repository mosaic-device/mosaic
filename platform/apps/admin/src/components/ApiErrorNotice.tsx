import type { ReactNode } from 'react'
import { CircleAlert } from 'lucide-react'
import { cn } from 'cn'
import { describeError } from '@/api/describe-error'

/**
 * Shows an API error (or a bug) as one plain message, with optional advice
 * from the page below it and an optional `action` on the right, such as a
 * dismiss button. Renders nothing for `null`.
 */
function ApiErrorNotice({
  error,
  className,
  children,
  action,
}: {
  error: unknown
  className?: string
  children?: ReactNode
  action?: ReactNode
}) {
  if (error === null || error === undefined) return null
  const { message, reference } = describeError(error)

  return (
    <div
      role="alert"
      className={cn(
        'border-danger bg-danger/10 flex gap-3 rounded-md border px-4 py-3',
        className,
      )}
    >
      <CircleAlert
        aria-hidden="true"
        className="text-danger mt-0.5 size-5 shrink-0"
      />
      <div className="grid min-w-0 flex-1 gap-1">
        <p>{message}</p>
        {children}
        {reference && (
          <p className="text-muted-foreground text-caption break-all">
            Reference: {reference}
          </p>
        )}
      </div>
      {action}
    </div>
  )
}

export { ApiErrorNotice }
