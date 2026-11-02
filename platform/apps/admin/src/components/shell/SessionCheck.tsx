import type { ErrorComponentProps } from '@tanstack/react-router'
import { PageError } from '@/components/PageError'
import { PublicPageLoading } from '@/components/PublicPageLoading'
import { PublicFrame } from '@/components/shell/PublicLayout'

// Shown in place of the shell until the server confirms the session, so no
// signed-in page or navigation appears before then.

function SessionCheckLoading() {
  return (
    <PublicFrame>
      <PublicPageLoading />
    </PublicFrame>
  )
}

function SessionCheckError(props: ErrorComponentProps) {
  return (
    <PublicFrame>
      <PageError {...props} />
    </PublicFrame>
  )
}

export { SessionCheckError, SessionCheckLoading }
