import {
  CircleCheck,
  CircleX,
  Clock,
  LoaderCircle,
  TriangleAlert,
  type LucideIcon,
} from 'lucide-react'
import type { ReactNode } from 'react'
import { cn } from 'cn'
import type { TestNotificationOutcome } from '@/api/devices'

type Tone = 'neutral' | 'success' | 'warning' | 'danger'

/** What the server said, or `busy` while another test to the device waits. */
type TestBannerResult = TestNotificationOutcome | 'busy'

const toneClasses: Record<Tone, { box: string; icon: string }> = {
  neutral: {
    box: 'border-border bg-surface-raised',
    icon: 'text-muted-foreground',
  },
  success: { box: 'border-success bg-success/10', icon: 'text-success' },
  warning: { box: 'border-warning bg-warning/10', icon: 'text-warning' },
  danger: { box: 'border-danger bg-danger/10', icon: 'text-danger' },
}

/**
 * Where a device's test notification stands: waiting for the device, what
 * the server said happened to it, or that another one is still waiting.
 * Renders nothing before the first send. The words carry the meaning; color
 * and icon only repeat it.
 */
function TestNotificationBanner({
  name,
  pending,
  result,
  className,
  action,
}: {
  name: string
  pending: boolean
  result: TestBannerResult | null
  className?: string
  /** Shown on finished results only, such as a dismiss button. */
  action?: ReactNode
}) {
  if (pending) {
    return (
      <Banner tone="neutral" icon={LoaderCircle} spin className={className}>
        Waiting for {name} to answer…
      </Banner>
    )
  }
  switch (result) {
    case null:
      return null
    case 'delivered':
      return (
        <Banner
          tone="success"
          icon={CircleCheck}
          className={className}
          action={action}
        >
          Delivered. {name} confirmed it got the test notification.
        </Banner>
      )
    case 'offline':
      return (
        <Banner
          tone="warning"
          icon={TriangleAlert}
          className={className}
          action={action}
        >
          Not sent. {name} is offline. Check that it's on and connected to
          Wi-Fi.
        </Banner>
      )
    case 'no_answer':
      return (
        <Banner
          tone="warning"
          icon={TriangleAlert}
          className={className}
          action={action}
        >
          No answer. {name} didn't confirm within 10 seconds, so it may not have
          arrived.
        </Banner>
      )
    case 'rejected':
      return (
        <Banner
          tone="danger"
          icon={CircleX}
          className={className}
          action={action}
        >
          {name} refused the test notification. This is a server problem, and
          the server logged it.
        </Banner>
      )
    case 'busy':
      return (
        <Banner
          tone="neutral"
          icon={Clock}
          className={className}
          action={action}
        >
          Another test notification to {name} is still waiting for an answer.
        </Banner>
      )
  }
}

function Banner({
  tone,
  icon: Icon,
  spin = false,
  className,
  action,
  children,
}: {
  tone: Tone
  icon: LucideIcon
  spin?: boolean
  className?: string
  action?: ReactNode
  children: ReactNode
}) {
  return (
    <div
      className={cn(
        'flex gap-3 rounded-md border px-4 py-3',
        toneClasses[tone].box,
        className,
      )}
    >
      <Icon
        aria-hidden="true"
        className={cn(
          'mt-0.5 size-5 shrink-0',
          toneClasses[tone].icon,
          spin && 'animate-spin',
        )}
      />
      <p className="min-w-0 flex-1">{children}</p>
      {action}
    </div>
  )
}

export { TestNotificationBanner, type TestBannerResult }
