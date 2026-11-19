import { Link, type LinkProps } from '@tanstack/react-router'
import { ArrowLeft } from 'lucide-react'
import type { ReactNode } from 'react'
import { usePageTitle } from '@/lib/use-page-title'

function PageHeader({
  title,
  description,
  actions,
  back,
  documentTitle,
}: {
  title: string
  description?: string
  actions?: ReactNode
  /** A sub-page's way back to its parent, shown above the title. */
  back?: { to: LinkProps['to']; label: string }
  documentTitle?: string
}) {
  usePageTitle(documentTitle ?? title)

  return (
    <header className="flex flex-wrap items-start justify-between gap-4">
      <div className="grid gap-2">
        {back && (
          <Link
            to={back.to}
            aria-label={`Back to ${back.label}`}
            // The padding makes a 44px target without adding height.
            className="text-muted-foreground hover:text-foreground -my-3 inline-flex items-center gap-1.5 justify-self-start py-3 text-sm"
          >
            <ArrowLeft aria-hidden="true" className="size-4" />
            {back.label}
          </Link>
        )}
        <h1 className="text-title leading-tight">{title}</h1>
        {description && <p className="text-muted-foreground">{description}</p>}
      </div>
      {actions}
    </header>
  )
}

export { PageHeader }
