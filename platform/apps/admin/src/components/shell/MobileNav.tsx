import { useEffect, useState } from 'react'
import { useRouter } from '@tanstack/react-router'
import { Menu } from 'lucide-react'
import { NavLinks } from '@/components/shell/NavLinks'
import { Wordmark } from '@/components/shell/Wordmark'
import { Button } from '@/components/ui/button'
import {
  Sheet,
  SheetContent,
  SheetTitle,
  SheetTrigger,
} from '@/components/ui/sheet'

function MobileNav() {
  const router = useRouter()
  const [open, setOpen] = useState(false)

  // Any page change closes the menu, including Back and Forward.
  useEffect(
    () => router.subscribe('onBeforeNavigate', () => setOpen(false)),
    [router],
  )

  return (
    <Sheet open={open} onOpenChange={setOpen}>
      <SheetTrigger asChild>
        <Button
          variant="ghost"
          size="icon"
          aria-label="Open menu"
          className="min-h-control-touch min-w-control-touch"
        >
          <Menu aria-hidden="true" />
        </Button>
      </SheetTrigger>
      <SheetContent aria-describedby={undefined}>
        <SheetTitle className="sr-only">Menu</SheetTitle>
        <Wordmark className="px-3 py-3 text-lg" />
        <nav aria-label="Main">
          <NavLinks onNavigate={() => setOpen(false)} />
        </nav>
      </SheetContent>
    </Sheet>
  )
}

export { MobileNav }
