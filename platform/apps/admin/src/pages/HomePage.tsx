import { getRouteApi, Link, useRouter } from '@tanstack/react-router'
import { Bell, GitPullRequest, Monitor, Plug } from 'lucide-react'
import type { ReactNode } from 'react'
import type { Device } from '@/api/devices'
import { EmptyState } from '@/components/EmptyState'
import { PageHeader } from '@/components/PageHeader'
import { StatusIndicator, type StatusTone } from '@/components/StatusIndicator'
import {
  Card,
  CardContent,
  CardDescription,
  CardHeader,
} from '@/components/ui/card'
import { useDeviceList } from '@/lib/use-device-list'

const route = getRouteApi('/app/')

const dateFormat = new Intl.DateTimeFormat(undefined, { dateStyle: 'medium' })

// Placeholder states until integrations (M4, M5) report real ones.
const connections = [
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
        <DeviceCard />
        {connections.map(({ label, icon, status, detail }) => (
          <ConnectionCard
            key={label}
            label={label}
            icon={icon}
            tone="neutral"
            status={status}
            detail={detail}
          />
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

/**
 * The paired devices at a glance, kept current like the Devices page: every
 * 10 seconds while the tab is visible, and when the owner comes back to it.
 */
function DeviceCard() {
  const router = useRouter()
  const { devices } = useDeviceList(route.useLoaderData(), () => {
    // The route guard asks again and opens login with a way back here.
    void router.invalidate()
  })
  const online = devices.filter((device) => device.connected).length

  if (devices.length === 0) {
    return (
      <ConnectionCard
        label="Device"
        icon={Monitor}
        tone="neutral"
        status="Not paired"
        detail="No device is paired with this server."
        link={<CardLink to="/devices/pair">Pair a device</CardLink>}
      />
    )
  }
  const [only] = devices
  return (
    <ConnectionCard
      label="Device"
      icon={Monitor}
      tone={online > 0 ? 'success' : 'neutral'}
      status={
        devices.length === 1
          ? online > 0
            ? 'Online'
            : 'Offline'
          : `${online} of ${devices.length} online`
      }
      detail={
        devices.length === 1 && only ? (
          <PairedDevice device={only} />
        ) : (
          `${devices.length} devices are paired with this server.`
        )
      }
      link={<CardLink to="/devices">View devices</CardLink>}
    />
  )
}

/** Devices have no names yet, so "Mosaic" plus the ID, as on Devices. */
function PairedDevice({ device }: { device: Device }) {
  return (
    <>
      Mosaic {device.id}, paired{' '}
      <time dateTime={device.pairedAt}>
        {dateFormat.format(new Date(device.pairedAt))}
      </time>
      .
    </>
  )
}

function ConnectionCard({
  label,
  icon: Icon,
  tone,
  status,
  detail,
  link,
}: {
  label: string
  icon: typeof Monitor
  tone: StatusTone
  status: string
  detail: ReactNode
  link?: ReactNode
}) {
  return (
    <Card className="min-w-0 flex-[1_1_240px] py-5">
      <CardContent className="grid gap-3 px-5">
        <h2 className="flex items-center gap-2 text-base font-medium">
          <Icon aria-hidden="true" className="text-muted-foreground size-5" />
          {label}
        </h2>
        <StatusIndicator tone={tone}>{status}</StatusIndicator>
        <p className="text-muted-foreground text-sm">{detail}</p>
        {link}
      </CardContent>
    </Card>
  )
}

function CardLink({
  to,
  children,
}: {
  to: '/devices' | '/devices/pair'
  children: ReactNode
}) {
  return (
    <Link
      to={to}
      // The padding makes a 44px target without adding height.
      className="-my-3 justify-self-start py-3 text-sm font-medium underline underline-offset-4"
    >
      {children}
    </Link>
  )
}

export { HomePage }
