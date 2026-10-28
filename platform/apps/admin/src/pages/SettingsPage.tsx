import { SlidersHorizontal } from 'lucide-react'
import { EmptyState } from '@/components/EmptyState'
import { PageHeader } from '@/components/PageHeader'
import { Card, CardContent } from '@/components/ui/card'

function SettingsPage() {
  return (
    <>
      <PageHeader title="Settings" />
      <Card>
        <CardContent>
          <EmptyState
            icon={SlidersHorizontal}
            title="Nothing to set up yet"
            description="Settings for this server will show up here."
          />
        </CardContent>
      </Card>
    </>
  )
}

export { SettingsPage }
