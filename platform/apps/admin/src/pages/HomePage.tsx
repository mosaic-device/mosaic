import { Bell, GitPullRequest, Monitor, Plug } from 'lucide-react'
import { EmptyState } from '@/components/EmptyState'
import { PageHeader } from '@/components/PageHeader'
import { StatusIndicator } from '@/components/StatusIndicator'
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
} from '@/components/ui/card'

// Placeholder states until pairing (M3) and integrations (M4, M5) report real ones.
const connections = [
  {
    label: 'Device',
    icon: Monitor,
    status: 'Not paired',
    detail: 'No device is paired with this server.',
  },
  {
    label: 'GitHub',
    icon: GitPullRequest,
    status: 'Not connected',
    detail: "GitHub isn't connected to this server.",
  },
  {
    label: 'Second integration',
    icon: Plug,
    status: 'Not connected',
    detail: 'No second service is connected.',
  },
] as const

function HomePage() {
  return (
    <>
      <PageHeader title="Welcome" documentTitle="Home" />
      <div className="flex flex-wrap gap-4">
        {connections.map(({ label, icon: Icon, status, detail }) => (
          <Card key={label} className="min-w-0 flex-[1_1_240px] py-5">
            <CardContent className="grid gap-3 px-5">
              <h2 className="flex items-center gap-2 text-base font-medium">
                <Icon
                  aria-hidden="true"
                  className="text-muted-foreground size-5"
                />
                {label}
              </h2>
              <StatusIndicator tone="neutral">{status}</StatusIndicator>
              <p className="text-muted-foreground text-sm">{detail}</p>
            </CardContent>
          </Card>
        ))}
      </div>
      <Card>
        <CardHeader>
          <h2 className="text-heading">Recent notifications</h2>
          <CardDescription>Sent to your device</CardDescription>
        </CardHeader>
        <CardContent>
          <EmptyState
            icon={Bell}
            title="No notifications yet"
            description="Notifications sent to your device will show up here."
            headingLevel={3}
          />
        </CardContent>
      </Card>
    </>
  )
}

export { HomePage }
