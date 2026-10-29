import { Link } from '@tanstack/react-router'
import { PageHeader } from '@/components/PageHeader'
import { Button } from '@/components/ui/button'

function NotFoundPage() {
  return (
    <>
      <PageHeader
        title="Page not found"
        description="Check the address, or head back to Home."
      />
      <div>
        <Button asChild variant="secondary">
          <Link to="/">Go to Home</Link>
        </Button>
      </div>
    </>
  )
}

export { NotFoundPage }
