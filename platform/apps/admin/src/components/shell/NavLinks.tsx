import { Link, useMatchRoute } from '@tanstack/react-router'
import { House, Monitor, SlidersHorizontal } from 'lucide-react'

// `subpages` also mark a section current.
const NAV_ITEMS = [
  { to: '/', label: 'Home', icon: House, subpages: [] },
  {
    to: '/devices',
    label: 'Devices',
    icon: Monitor,
    subpages: ['/devices/pair'],
  },
  { to: '/settings', label: 'Settings', icon: SlidersHorizontal, subpages: [] },
] as const

function NavLinks({ onNavigate }: { onNavigate?: () => void }) {
  const matchRoute = useMatchRoute()

  return (
    <ul className="grid gap-1">
      {NAV_ITEMS.map(({ to, label, icon: Icon, subpages }) => (
        <li key={to}>
          <Link
            to={to}
            // Exact, plus the listed subpages, so "/" or a not-found page
            // under /devices doesn't mark a section current.
            activeOptions={{ exact: true }}
            aria-current={
              subpages.some((page) => matchRoute({ to: page }))
                ? 'page'
                : undefined
            }
            onClick={onNavigate}
            // The current page gets a dark shade of the primary purple, so it
            // stands out without competing with primary buttons.
            className="min-h-control-md [@media(max-width:640px)]:min-h-control-touch hover:bg-secondary-hover aria-[current=page]:text-foreground [&[aria-current=page]>svg]:text-selected-foreground flex items-center gap-3 rounded-md border border-transparent px-3 text-sm font-medium aria-[current=page]:bg-[color-mix(in_srgb,var(--color-primary)_34%,var(--color-surface))] forced-colors:aria-[current=page]:outline-2 forced-colors:aria-[current=page]:outline-offset-[-3px] forced-colors:aria-[current=page]:outline-[Highlight]"
          >
            <Icon aria-hidden="true" className="size-5" />
            {label}
          </Link>
        </li>
      ))}
    </ul>
  )
}

export { NavLinks }
