import type { ReactNode } from 'react'
import { usePageTitle } from '@/lib/use-page-title'

function PageHeader({
  title,
  description,
  actions,
  documentTitle,
}: {
  title: string
  description?: string
  actions?: ReactNode
  documentTitle?: string
}) {
  usePageTitle(documentTitle ?? title)

  return (
    <header className="flex flex-wrap items-start justify-between gap-4">
      <div className="grid gap-2">
        <h1 className="text-title leading-tight">{title}</h1>
        {description && <p className="text-muted-foreground">{description}</p>}
      </div>
      {actions}
    </header>
  )
}

export { PageHeader }
