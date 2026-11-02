import type { ReactNode } from 'react'
import { PageHeader } from '@/components/PageHeader'
import { Wordmark } from '@/components/shell/Wordmark'
import { Card, CardContent, CardHeader } from '@/components/ui/card'

/** The frame for setup and login, outside the signed-in shell. */
function AuthCard({
  title,
  description,
  documentTitle,
  children,
}: {
  title: string
  description: string
  documentTitle?: string
  children: ReactNode
}) {
  return (
    <div className="grid w-full max-w-sm gap-6">
      <Wordmark className="justify-self-center text-lg" />
      <Card>
        <CardHeader>
          <PageHeader
            title={title}
            description={description}
            documentTitle={documentTitle}
          />
        </CardHeader>
        <CardContent>{children}</CardContent>
      </Card>
    </div>
  )
}

export { AuthCard }
