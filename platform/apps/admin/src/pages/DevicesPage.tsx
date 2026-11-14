import { getRouteApi, Link, useRouter } from '@tanstack/react-router'
import { CircleCheck, Monitor, Plus } from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import { removeDevice, type Device } from '@/api/devices'
import type { ApiError } from '@/api/errors'
import { useApiAction } from '@/api/use-api-action'
import { ApiErrorNotice } from '@/components/ApiErrorNotice'
import { EmptyState } from '@/components/EmptyState'
import { PageHeader } from '@/components/PageHeader'
import { PairedAt } from '@/components/PairedAt'
import { StatusIndicator } from '@/components/StatusIndicator'
import { Button } from '@/components/ui/button'
import { Card, CardContent } from '@/components/ui/card'
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@/components/ui/dialog'
import { useDeviceList, type LoadedDevices } from '@/lib/use-device-list'

const route = getRouteApi('/app/devices')

const reportedAtFormat = new Intl.DateTimeFormat(undefined, {
  timeStyle: 'short',
})

/** Devices have no names yet (Admin API Contract, section 5). */
function deviceName(device: Device): string {
  return `Mosaic ${device.id}`
}

function DevicesPage() {
  const loaded = route.useLoaderData()

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
      <DeviceList loaded={loaded} />
    </>
  )
}

function DeviceList({ loaded }: { loaded: LoadedDevices }) {
  const router = useRouter()
  const { devices, reportedAt, refreshError, forget } = useDeviceList(
    loaded,
    // The route guard asks again and opens login with a way back here.
    () => void router.invalidate(),
  )
  const [removing, setRemoving] = useState<Device | null>(null)
  const [removed, setRemoved] = useState<Device | null>(null)
  const removedRef = useRef<HTMLParagraphElement>(null)
  const justRemoved = useRef(false)
  // The Remove button that opened the dialog. Not every browser focuses a
  // button on click, so focus is put back by hand.
  const opener = useRef<HTMLElement | null>(null)

  return (
    <div className="grid gap-4">
      {removed && (
        // Focused after a removal, since the button that started it is gone.
        <p
          ref={removedRef}
          tabIndex={-1}
          className="border-success bg-success/10 flex items-center gap-3 rounded-md border px-4 py-3"
        >
          <CircleCheck
            aria-hidden="true"
            className="text-success size-5 shrink-0"
          />
          {deviceName(removed)} removed.
        </p>
      )}
      <ApiErrorNotice error={refreshError}>
        <p>
          Online and offline show what the server last reported, at{' '}
          {reportedAtFormat.format(reportedAt)}.
        </p>
      </ApiErrorNotice>
      {devices.length === 0 ? (
        <Card>
          <CardContent>
            <EmptyState
              icon={Monitor}
              title="No devices yet"
              description="When you pair a Mosaic, it shows up here with whether it's online."
            />
          </CardContent>
        </Card>
      ) : (
        <Card className="py-2">
          <CardContent className="@container">
            <ul aria-label="Paired devices" className="divide-border divide-y">
              {devices.map((device) => (
                <DeviceRow
                  key={device.id}
                  device={device}
                  onRemove={(button) => {
                    opener.current = button
                    setRemoving(device)
                  }}
                />
              ))}
            </ul>
          </CardContent>
        </Card>
      )}
      <RemoveDeviceDialog
        device={removing}
        onClose={() => setRemoving(null)}
        onRemoved={(device) => {
          forget(device.id)
          setRemoved(device)
          justRemoved.current = true
          setRemoving(null)
        }}
        onCloseAutoFocus={(event) => {
          const target = justRemoved.current
            ? removedRef.current
            : opener.current
          justRemoved.current = false
          opener.current = null
          // A refresh may have dropped the row meanwhile; then Radix decides.
          if (!target?.isConnected) return
          event.preventDefault()
          target.focus()
        }}
      />
    </div>
  )
}

/**
 * Name, status and paired date, with Remove on the right. Stacked beside
 * the icon in a narrow card; the status moves next to Remove in a wide one.
 */
function DeviceRow({
  device,
  onRemove,
}: {
  device: Device
  onRemove: (button: HTMLButtonElement) => void
}) {
  const name = deviceName(device)

  return (
    <li className="grid grid-cols-[auto_minmax(0,1fr)_auto] items-center gap-x-4 gap-y-1 py-4 @md:grid-cols-[auto_minmax(0,1fr)_auto_auto]">
      <div
        aria-hidden="true"
        className="bg-surface-raised text-muted-foreground row-span-3 grid size-10 place-items-center self-start rounded-md @md:row-span-2 @md:self-center"
      >
        <Monitor className="size-5" />
      </div>
      <h2 className="col-start-2 font-medium">{name}</h2>
      <div className="col-start-2 @md:col-start-3 @md:row-span-2 @md:row-start-1">
        <StatusIndicator tone={device.connected ? 'success' : 'neutral'}>
          {device.connected ? 'Online' : 'Offline'}
        </StatusIndicator>
      </div>
      <PairedAt value={device.pairedAt} className="col-start-2" />
      <Button
        variant="secondary"
        aria-label={`Remove ${name}`}
        className="col-start-3 row-span-3 row-start-1 self-start @md:col-start-4 @md:row-span-2 @md:self-center"
        onClick={(event) => onRemove(event.currentTarget)}
      >
        Remove
      </Button>
    </li>
  )
}

/**
 * Confirms a removal. It stays open while the server is asked, so the answer
 * is never missed. A `device_not_found` answer means the device is already
 * gone, which counts as removed.
 */
function RemoveDeviceDialog({
  device,
  onClose,
  onRemoved,
  onCloseAutoFocus,
}: {
  device: Device | null
  onClose: () => void
  onRemoved: (device: Device) => void
  onCloseAutoFocus: (event: Event) => void
}) {
  const router = useRouter()
  const { run, pending } = useApiAction(removeDevice)
  const [failure, setFailure] = useState<ApiError | null>(null)
  const confirmRef = useRef<HTMLButtonElement>(null)
  // Keeps the name while the dialog closes.
  const [shown, setShown] = useState(device)
  if (device && device !== shown) setShown(device)

  function close() {
    if (pending) return
    setFailure(null)
    onClose()
  }

  async function remove() {
    if (!device || pending) return
    setFailure(null)
    const result = await run(device.id)
    if (result.status === 'cancelled') return
    if (
      result.status === 'success' ||
      result.error.code === 'device_not_found'
    ) {
      onRemoved(device)
      return
    }
    if (result.error.code === 'unauthenticated') {
      // Login opens with a way back. If the session turns out fine after
      // all, the dialog stays and says so.
      await router.invalidate()
    }
    setFailure(result.error)
  }

  // The pending button lost focus when it was disabled; give it back so the
  // owner can retry from the keyboard.
  useEffect(() => {
    if (failure) confirmRef.current?.focus()
  }, [failure])

  const name = shown ? deviceName(shown) : ''

  return (
    <Dialog
      open={device !== null}
      onOpenChange={(open) => {
        if (!open) close()
      }}
    >
      <DialogContent
        role="alertdialog"
        showCloseButton={false}
        onCloseAutoFocus={onCloseAutoFocus}
        // A confirmation closes only through its buttons or Escape.
        onInteractOutside={(event) => event.preventDefault()}
      >
        <DialogHeader>
          {/* No close button to make room for, so no right padding. */}
          <DialogTitle className="pr-0">Remove {name}?</DialogTitle>
          <DialogDescription>
            It disconnects and stops working with this server right away, even
            if it's offline now. To use it again, pair it with the new code it
            shows.
          </DialogDescription>
        </DialogHeader>
        <ApiErrorNotice error={failure}>
          {failure && mayHaveRemoved(failure) && (
            <p>The server didn't confirm the removal. Try again.</p>
          )}
        </ApiErrorNotice>
        <DialogFooter>
          {/* First, so it has focus when the dialog opens. */}
          <Button variant="secondary" disabled={pending} onClick={close}>
            Cancel
          </Button>
          <Button
            ref={confirmRef}
            variant="destructive"
            pending={pending}
            onClick={() => void remove()}
          >
            {pending ? 'Removing…' : 'Remove device'}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  )
}

/**
 * True when the removal may have gone through without the app hearing back.
 * Retrying is safe: the server then answers `device_not_found`.
 */
function mayHaveRemoved(error: ApiError): boolean {
  return error.kind !== 'problem' || error.code === 'internal_error'
}

export { DevicesPage }
