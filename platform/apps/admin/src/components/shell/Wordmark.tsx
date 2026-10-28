import { cn } from 'cn'

function Wordmark({ className }: { className?: string }) {
  return (
    <span
      // The shared font allowlist omits the configured wordmark-only brand family.
      // eslint-disable-next-line no-restricted-syntax
      className={cn('font-brand font-medium tracking-[0.23em]', className)}
    >
      MOSAIC
    </span>
  )
}

export { Wordmark }
