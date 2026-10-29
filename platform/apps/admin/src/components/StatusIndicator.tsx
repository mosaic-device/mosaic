import type { ReactNode } from 'react'
import { cn } from 'cn'

type StatusTone = 'neutral' | 'success' | 'warning' | 'danger'

const dotClasses: Record<StatusTone, string> = {
  neutral: 'bg-muted-foreground',
  success: 'bg-success',
  warning: 'bg-warning',
  danger: 'bg-danger',
}

function StatusIndicator({
  tone,
  children,
}: {
  tone: StatusTone
  children: ReactNode
}) {
  return (
    <span className="inline-flex items-center gap-2 text-sm font-medium">
      <span
        aria-hidden="true"
        className={cn(
          'size-2 shrink-0 rounded-full forced-colors:bg-[CanvasText]',
          dotClasses[tone],
        )}
      />
      {children}
    </span>
  )
}

export { StatusIndicator, type StatusTone }
