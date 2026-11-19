import { X } from 'lucide-react'
import { Button } from '@/components/ui/button'

/**
 * A small X that closes a message. Its hover tint works on any banner color,
 * and the negative margins keep a 48px touch target from making the banner
 * taller. Callers move focus somewhere sensible, since the button goes away.
 */
function DismissButton({
  label,
  onClick,
}: {
  label: string
  onClick: () => void
}) {
  return (
    <Button
      variant="ghost"
      size="icon-sm"
      aria-label={label}
      onClick={onClick}
      className="hover:bg-foreground/10 active:bg-foreground/15 [@media(max-width:640px)]:min-w-control-touch -my-1 -mr-2 shrink-0 self-start [@media(max-width:640px)]:-my-3"
    >
      <X aria-hidden="true" />
    </Button>
  )
}

export { DismissButton }
