import { Link } from '@tanstack/react-router'
import { House, Monitor, SlidersHorizontal } from 'lucide-react'

const NAV_ITEMS = [
  { to: '/', label: 'Home', icon: House },
  { to: '/devices', label: 'Devices', icon: Monitor },
  { to: '/settings', label: 'Settings', icon: SlidersHorizontal },
] as const

function NavLinks({ onNavigate }: { onNavigate?: () => void }) {
  return (
    <ul className="grid gap-1">
      {NAV_ITEMS.map(({ to, label, icon: Icon }) => (
        <li key={to}>
          <Link
            to={to}
            // Exact until nested pages exist, so "/" or a not-found page under
            // /devices doesn't mark a section current. Revisit with /devices/$id (M3).
            activeOptions={{ exact: true }}
            onClick={onNavigate}
            className="min-h-control-md [@media(max-width:640px)]:min-h-control-touch hover:bg-secondary-hover aria-[current=page]:border-selected-border aria-[current=page]:bg-selected aria-[current=page]:text-selected-foreground flex items-center gap-3 rounded-md border border-transparent px-3 text-sm font-medium forced-colors:aria-[current=page]:outline-2 forced-colors:aria-[current=page]:outline-offset-[-3px] forced-colors:aria-[current=page]:outline-[Highlight]"
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
