import { cn } from 'cn'

const dateFormat = new Intl.DateTimeFormat(undefined, { dateStyle: 'medium' })
const timeFormat = new Intl.DateTimeFormat(undefined, { timeStyle: 'short' })

/**
 * "Paired" and the date and time, from the server's ISO 8601 timestamp.
 * `timeClassName` can hide the time where space is short.
 */
function PairedAt({
  value,
  className,
  timeClassName,
}: {
  value: string
  className?: string
  timeClassName?: string
}) {
  const pairedAt = new Date(value)

  return (
    <p className={cn('text-muted-foreground text-sm', className)}>
      Paired{' '}
      <time dateTime={value}>
        {dateFormat.format(pairedAt)}
        <span className={timeClassName}>, {timeFormat.format(pairedAt)}</span>
      </time>
    </p>
  )
}

export { PairedAt }
