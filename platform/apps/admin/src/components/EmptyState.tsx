import type { LucideIcon } from 'lucide-react'

function EmptyState({
  icon: Icon,
  title,
  description,
  headingLevel = 2,
}: {
  icon: LucideIcon
  title: string
  description: string
  headingLevel?: 2 | 3
}) {
  const Heading = headingLevel === 3 ? 'h3' : 'h2'

  return (
    <div className="grid justify-items-center gap-3 px-6 py-10 text-center">
      <div
        aria-hidden="true"
        className="bg-surface-raised text-muted-foreground grid size-14 place-items-center rounded-full"
      >
        <Icon className="size-6" />
      </div>
      <Heading className="text-heading">{title}</Heading>
      <p className="text-muted-foreground max-w-md">{description}</p>
    </div>
  )
}

export { EmptyState }
