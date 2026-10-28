import { useState } from 'react'
import { useRouterState } from '@tanstack/react-router'
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
  const pathname = useRouterState({
    select: (state) => state.location.pathname,
  })
  // Remember which page the menu opened on, so any page change (including
  // Back and Forward) closes it.
  const [openedOn, setOpenedOn] = useState<string | null>(null)

  return (
    <Sheet
      open={openedOn === pathname}
      onOpenChange={(open) => setOpenedOn(open ? pathname : null)}
    >
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
          <NavLinks onNavigate={() => setOpenedOn(null)} />
        </nav>
      </SheetContent>
    </Sheet>
  )
}

export { MobileNav }
