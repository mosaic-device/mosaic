import { Link, useRouter } from '@tanstack/react-router'
import { useEffect, useRef, useState, type FormEvent } from 'react'
import { claimDevice, validateClaim, type Device } from '@/api/devices'
import type { ApiError, ApiErrorCode } from '@/api/errors'
import { useApiAction } from '@/api/use-api-action'
import { ApiErrorNotice } from '@/components/ApiErrorNotice'
import { PageHeader } from '@/components/PageHeader'
import { StatusIndicator } from '@/components/StatusIndicator'
import { TextField } from '@/components/TextField'
import { Button } from '@/components/ui/button'
import { Card, CardContent } from '@/components/ui/card'
import {
  hasMessages,
  useCredentialForm,
  waitLabel,
} from '@/lib/use-credential-form'

const fields = ['code'] as const

/** Problems with the code itself: the fix is to type the right one. */
const CODE_PROBLEMS: readonly ApiErrorCode[] = [
  'invalid_pairing_code',
  'pairing_code_expired',
  'pairing_code_used',
]

const pairedAtFormat = new Intl.DateTimeFormat(undefined, {
  dateStyle: 'medium',
  timeStyle: 'short',
})

function PairDevicePage() {
  const [paired, setPaired] = useState<Device | null>(null)
  const [pairingAnother, setPairingAnother] = useState(false)

  return (
    <>
      <PageHeader
        title="Pair a device"
        description="Enter the code your Mosaic shows to pair it with this server."
      />
      <Card className="max-w-lg">
        <CardContent>
          {paired ? (
            <PairedDevice
              device={paired}
              onPairAnother={() => {
                setPaired(null)
                setPairingAnother(true)
              }}
            />
          ) : (
            <PairForm focusOnMount={pairingAnother} onPaired={setPaired} />
          )}
        </CardContent>
      </Card>
    </>
  )
}

function PairForm({
  focusOnMount,
  onPaired,
}: {
  focusOnMount: boolean
  onPaired: (device: Device) => void
}) {
  const router = useRouter()
  const { run, pending } = useApiAction(claimDevice)
  const {
    clearMessages,
    fieldProps,
    formRef,
    notice,
    readValues,
    secondsLeft,
    showError,
    showFieldErrors,
  } = useCredentialForm(fields)

  function codeInput(): HTMLInputElement | null {
    const element = formRef.current?.elements.namedItem('code')
    return element instanceof HTMLInputElement ? element : null
  }

  async function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault()
    // A claim the server already received still happens if it is replaced,
    // so a second submit waits for the first to finish.
    if (pending || secondsLeft > 0) return
    const input = readValues()
    clearMessages()
    const problems = validateClaim(input)
    if (hasMessages(problems)) {
      showFieldErrors(problems)
      return
    }

    const result = await run(input)
    if (result.status === 'cancelled') return
    if (result.status === 'success') {
      onPaired(result.data.device)
      return
    }
    const { error } = result
    if (error.code === 'unauthenticated') {
      // The route guard asks again and opens login with a way back here. If
      // the session turns out fine after all, the page stays and says so.
      await router.invalidate()
      showError(error)
      return
    }
    if (error.code && CODE_PROBLEMS.includes(error.code)) {
      showFieldErrors({ code: error.title })
      codeInput()?.select()
      return
    }
    showError(error)
  }

  return (
    <form
      ref={formRef}
      noValidate
      className="grid gap-5"
      onSubmit={(event) => void handleSubmit(event)}
    >
      <ApiErrorNotice error={notice}>
        {notice && mayHavePaired(notice) && (
          <p>
            The server didn't confirm the pairing. Try again with the same code;
            it won't pair the device twice.
          </p>
        )}
      </ApiErrorNotice>
      <TextField
        id="pair-code"
        label="Pairing code"
        hint="Shown on the Mosaic's screen, for example K7QF-2M9X."
        autoComplete="off"
        autoCapitalize="characters"
        autoCorrect="off"
        spellCheck={false}
        // Only after "Pair another device", not on arrival.
        autoFocus={focusOnMount}
        className="uppercase"
        required
        {...fieldProps('code')}
      />
      <Button
        type="submit"
        className="justify-self-start"
        pending={pending}
        disabled={secondsLeft > 0}
      >
        {pending
          ? 'Pairing…'
          : secondsLeft > 0
            ? waitLabel(secondsLeft)
            : 'Pair device'}
      </Button>
    </form>
  )
}

/**
 * True when the claim may have gone through without the app hearing back:
 * the server was out of reach, its answer broke the contract, or it failed
 * unexpectedly. Retrying the same code is safe either way.
 */
function mayHavePaired(error: ApiError): boolean {
  return error.kind !== 'problem' || error.code === 'internal_error'
}

function PairedDevice({
  device,
  onPairAnother,
}: {
  device: Device
  onPairAnother: () => void
}) {
  const headingRef = useRef<HTMLHeadingElement>(null)

  useEffect(() => {
    headingRef.current?.focus()
  }, [])

  return (
    <div className="grid gap-5">
      <div className="grid gap-3">
        {/* Focused so screen readers announce the result. */}
        <h2 ref={headingRef} tabIndex={-1} className="text-heading">
          Mosaic paired
        </h2>
        <StatusIndicator tone={device.connected ? 'success' : 'neutral'}>
          {device.connected ? 'Online' : 'Offline'}
        </StatusIndicator>
        <p className="text-muted-foreground text-sm">
          Paired{' '}
          <time dateTime={device.pairedAt}>
            {pairedAtFormat.format(new Date(device.pairedAt))}
          </time>
        </p>
        {!device.connected && (
          <p className="text-muted-foreground">
            It finishes pairing the next time it connects to this server.
          </p>
        )}
      </div>
      <div className="flex flex-wrap gap-3">
        <Button asChild>
          <Link to="/devices">Go to devices</Link>
        </Button>
        <Button variant="secondary" onClick={onPairAnother}>
          Pair another device
        </Button>
      </div>
    </div>
  )
}

export { PairDevicePage }
