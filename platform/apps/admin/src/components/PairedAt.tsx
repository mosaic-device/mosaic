import { cn } from 'cn'

const pairedAtFormat = new Intl.DateTimeFormat(undefined, {
  dateStyle: 'medium',
  timeStyle: 'short',
})

/** "Paired" and the date, from the server's ISO 8601 timestamp. */
function PairedAt({ value, className }: { value: string; className?: string }) {
  return (
    <p className={cn('text-muted-foreground text-sm', className)}>
      Paired{' '}
      <time dateTime={value}>{pairedAtFormat.format(new Date(value))}</time>
    </p>
  )
}

export { PairedAt }
