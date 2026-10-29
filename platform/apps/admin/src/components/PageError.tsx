import { useRouter, type ErrorComponentProps } from '@tanstack/react-router'
import { ApiErrorNotice } from '@/components/ApiErrorNotice'
import { PageHeader } from '@/components/PageHeader'
import { Button } from '@/components/ui/button'

/** The router's default error page: a failed loader or a bug in a page. */
function PageError({ error }: ErrorComponentProps) {
  const router = useRouter()

  return (
    <div className="grid gap-6">
      <PageHeader title="This page didn't load" documentTitle="Error" />
      <ApiErrorNotice error={error} />
      <div>
        <Button variant="secondary" onClick={() => void router.invalidate()}>
          Try again
        </Button>
      </div>
    </div>
  )
}

export { PageError }
