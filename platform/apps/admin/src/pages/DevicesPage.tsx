import { Link } from '@tanstack/react-router'
import { Monitor, Plus } from 'lucide-react'
import { EmptyState } from '@/components/EmptyState'
import { PageHeader } from '@/components/PageHeader'
import { Button } from '@/components/ui/button'
import { Card, CardContent } from '@/components/ui/card'

function DevicesPage() {
  return (
    <>
      <PageHeader
        title="Devices"
        description="Mosaic devices paired with this server."
        actions={
          <Button asChild>
            <Link to="/devices/pair">
              <Plus aria-hidden="true" />
              Pair a device
            </Link>
          </Button>
        }
      />
      <Card>
        <CardContent>
          <EmptyState
            icon={Monitor}
            title="No devices yet"
            description="When you pair a Mosaic, it shows up here with whether it's online."
          />
        </CardContent>
      </Card>
    </>
  )
}

export { DevicesPage }
