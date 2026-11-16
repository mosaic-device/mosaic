import { getRouteApi, Link, useRouter } from '@tanstack/react-router'
import {
  CircleCheck,
  Ellipsis,
  Info,
  Monitor,
  Plus,
  Send,
  Trash2,
} from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import {
  removeDevice,
  sendTestNotification,
  type Device,
  type TestNotificationOutcome,
} from '@/api/devices'
import type { ApiError } from '@/api/errors'
import { useApiAction } from '@/api/use-api-action'
import { ApiErrorNotice } from '@/components/ApiErrorNotice'
import { EmptyState } from '@/components/EmptyState'
import { PageHeader } from '@/components/PageHeader'
import { PairedAt } from '@/components/PairedAt'
import { StatusIndicator } from '@/components/StatusIndicator'
import { TestNotificationBanner } from '@/components/TestNotificationBanner'
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
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from '@/components/ui/dropdown-menu'
import { useCountdown } from '@/lib/use-countdown'
import { waitLabel } from '@/lib/use-credential-form'
import { useDeviceList, type LoadedDevices } from '@/lib/use-device-list'

const route = getRouteApi('/app/devices')

const reportedAtFormat = new Intl.DateTimeFormat(undefined, {
  timeStyle: 'short',
})

/** The longest `Retry-After` the server sends for a test notification. */
const MAX_TEST_WAIT_SECONDS = 10

/** Devices have no names yet (Admin API Contract, section 5). */
function deviceName(device: Device): string {
  return `Mosaic ${device.id}`
}

/** A message above the list, after a device left it. */
type PageNotice = { text: string; tone: 'success' | 'neutral' }

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
  const { devices, reportedAt, refreshError, forget, refresh } = useDeviceList(
    loaded,
    // The route guard asks again and opens login with a way back here.
    () => void router.invalidate(),
  )
  const [removing, setRemoving] = useState<Device | null>(null)
  const [notice, setNotice] = useState<PageNotice | null>(null)
  const noticeRef = useRef<HTMLParagraphElement>(null)
  const justRemoved = useRef(false)
  // Counts requests to focus the notice once it has rendered.
  const [noticeFocus, setNoticeFocus] = useState(0)
  // The menu button that opened the dialog. Not every browser focuses a
  // button on click, so focus is put back by hand.
  const opener = useRef<HTMLElement | null>(null)

  useEffect(() => {
    if (noticeFocus > 0) noticeRef.current?.focus()
  }, [noticeFocus])

  return (
    <div className="grid gap-4">
      {notice && (
        // Focused once its row is gone, since the menu that acted went too.
        <p
          ref={noticeRef}
          tabIndex={-1}
          className={
            notice.tone === 'success'
              ? 'border-success bg-success/10 flex items-center gap-3 rounded-md border px-4 py-3'
              : 'border-border bg-surface-raised flex items-center gap-3 rounded-md border px-4 py-3'
          }
        >
          {notice.tone === 'success' ? (
            <CircleCheck
              aria-hidden="true"
              className="text-success size-5 shrink-0"
            />
          ) : (
            <Info
              aria-hidden="true"
              className="text-muted-foreground size-5 shrink-0"
            />
          )}
          {notice.text}
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
                  onRemove={(menuButton) => {
                    opener.current = menuButton
                    setRemoving(device)
                  }}
                  onTested={refresh}
                  onUnpaired={() => {
                    forget(device.id)
                    setNotice({
                      text: `${deviceName(device)} isn't paired anymore, so nothing was sent.`,
                      tone: 'neutral',
                    })
                    setNoticeFocus((count) => count + 1)
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
          setNotice({ text: `${deviceName(device)} removed.`, tone: 'success' })
          justRemoved.current = true
          setRemoving(null)
        }}
        onCloseAutoFocus={(event) => {
          const target = justRemoved.current
            ? noticeRef.current
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
 * Name, status and paired date, with an actions menu (Send test, Remove) on
 * the right, and the latest test notification's result below. A narrow card
 * stacks them and leaves out the icon, so the text has room; a wide one adds
 * the icon and moves the status next to the menu.
 *
 * Each `@md:row-span-*` also sets `@md:row-start-*`: the span sets the whole
 * `grid-row`, which would otherwise drop a start line given without `@md:`.
 */
function DeviceRow({
  device,
  onRemove,
  onTested,
  onUnpaired,
}: {
  device: Device
  onRemove: (menuButton: HTMLElement | null) => void
  onTested: () => void
  onUnpaired: () => void
}) {
  const name = deviceName(device)
  const menuButton = useRef<HTMLButtonElement>(null)
  const test = useTestNotification(device, { onTested, onUnpaired })

  return (
    <li className="py-4">
      <div className="grid grid-cols-[minmax(0,1fr)_auto] items-center gap-x-4 gap-y-1 @md:grid-cols-[auto_minmax(0,1fr)_auto_auto]">
        <div
          aria-hidden="true"
          className="bg-surface-raised text-muted-foreground hidden size-10 place-items-center rounded-md @md:row-span-2 @md:row-start-1 @md:grid"
        >
          <Monitor className="size-5" />
        </div>
        {/* Sized like Home's card titles, below the page title. */}
        <h2 className="col-start-1 text-base font-medium @md:col-start-2">
          {name}
        </h2>
        {/* The minimum width lines the status dots up from row to row. */}
        <div className="col-start-1 @md:col-start-3 @md:row-span-2 @md:row-start-1 @md:min-w-20">
          <StatusIndicator tone={device.connected ? 'success' : 'neutral'}>
            {device.connected ? 'Online' : 'Offline'}
          </StatusIndicator>
        </div>
        {/* A narrow card shows the date only, so it fits on one line. */}
        <PairedAt
          value={device.pairedAt}
          className="col-start-1 @md:col-start-2"
          timeClassName="hidden @md:inline"
        />
        <DropdownMenu>
          <DropdownMenuTrigger asChild>
            <Button
              ref={menuButton}
              variant="ghost"
              size="icon"
              aria-label={`Actions for ${name}`}
              className="[@media(max-width:640px)]:min-w-control-touch col-start-2 row-span-3 row-start-1 self-start @md:col-start-4 @md:row-span-2 @md:row-start-1 @md:self-center"
            >
              <Ellipsis aria-hidden="true" />
            </Button>
          </DropdownMenuTrigger>
          <DropdownMenuContent align="end">
            <DropdownMenuItem
              disabled={!test.canSend}
              onSelect={() => void test.send()}
            >
              <Send aria-hidden="true" />
              {test.sendLabel}
            </DropdownMenuItem>
            <DropdownMenuItem
              variant="destructive"
              onSelect={() => onRemove(menuButton.current)}
            >
              <Trash2 aria-hidden="true" />
              Remove
            </DropdownMenuItem>
          </DropdownMenuContent>
        </DropdownMenu>
      </div>
      {/* Always there, so screen readers hear each change. */}
      <div role="status">
        <TestNotificationBanner
          name={name}
          pending={test.pending}
          result={test.result}
          className="mt-3"
        />
      </div>
      <ApiErrorNotice error={test.failure} className="mt-3">
        {test.failure && mayHaveGoneThrough(test.failure) && (
          <p>The app didn't hear whether it arrived.</p>
        )}
      </ApiErrorNotice>
    </li>
  )
}

type TestResult = TestNotificationOutcome | 'busy'

/**
 * Sends one test notification at a time and keeps what the server said. A
 * `200` is always an answer about the notification, even `offline`; the
 * other answers are the request failing. `device_not_found` means the device
 * was removed elsewhere, and `too_many_requests` that another test to it is
 * still waiting, so the next one waits out `Retry-After`.
 */
function useTestNotification(
  device: Device,
  { onTested, onUnpaired }: { onTested: () => void; onUnpaired: () => void },
) {
  const router = useRouter()
  const { run, pending } = useApiAction(sendTestNotification)
  const [result, setResult] = useState<TestResult | null>(null)
  const [failure, setFailure] = useState<ApiError | null>(null)
  const { secondsLeft, start } = useCountdown()

  async function send() {
    if (pending || secondsLeft > 0) return
    setResult(null)
    setFailure(null)
    const sent = await run(device.id)
    if (sent.status === 'cancelled') return
    if (sent.status === 'success') {
      setResult(sent.data.outcome)
      // So Online and Offline agree with what the test just found.
      onTested()
      return
    }
    switch (sent.error.code) {
      case 'device_not_found':
        onUnpaired()
        return
      case 'too_many_requests':
        setResult('busy')
        start(sent.error.retryAfterSeconds ?? MAX_TEST_WAIT_SECONDS)
        return
      case 'unauthenticated':
        // Login opens with a way back. If the session turns out fine after
        // all, the row says so.
        await router.invalidate()
    }
    setFailure(sent.error)
  }

  return {
    send,
    pending,
    result,
    failure,
    canSend: !pending && secondsLeft === 0,
    sendLabel: pending
      ? 'Sending…'
      : secondsLeft > 0
        ? waitLabel(secondsLeft)
        : 'Send test',
  }
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
          {failure && mayHaveGoneThrough(failure) && (
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
 * True when a removal or test notification may have gone through without
 * the app hearing back. Retrying a removal is safe: the server then answers
 * `device_not_found`.
 */
function mayHaveGoneThrough(error: ApiError): boolean {
  return error.kind !== 'problem' || error.code === 'internal_error'
}

export { DevicesPage }
