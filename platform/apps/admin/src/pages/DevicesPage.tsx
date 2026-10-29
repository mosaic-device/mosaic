import { Monitor } from 'lucide-react'
import { EmptyState } from '@/components/EmptyState'
import { PageHeader } from '@/components/PageHeader'
import { Card, CardContent } from '@/components/ui/card'

function DevicesPage() {
  return (
    <>
      <PageHeader
        title="Devices"
        description="Mosaic devices paired with this server."
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
