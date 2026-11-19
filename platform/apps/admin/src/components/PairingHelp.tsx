import { CircleCheck } from 'lucide-react'
import { useId } from 'react'
import type { Device } from '@/api/devices'
import { Wordmark } from '@/components/shell/Wordmark'

const STEPS = [
  'Plug in your Mosaic and finish its Wi-Fi setup.',
  "It shows an 8-character code and this server's address.",
  'Enter the code within 10 minutes. If it runs out, your Mosaic shows a new one.',
]

/**
 * Beside the pair form: where the code comes from, then what the device does
 * once paired. A device claimed while offline only shows it when it connects.
 */
function PairingHelp({ device }: { device: Device | null }) {
  const headingId = useId()
  const paired = device !== null

  return (
    <aside aria-labelledby={headingId} className="grid content-start gap-4">
      <h2 id={headingId} className="text-heading">
        {paired ? 'On your Mosaic' : 'Where to find the code'}
      </h2>
      <DeviceScreen paired={paired} />
      {paired ? (
        <p className="text-muted-foreground text-sm">
          {device.connected ? 'Your' : 'When it next connects, your'} Mosaic
          shows that it's paired, then opens its Home screen.
        </p>
      ) : (
        <ol className="text-muted-foreground grid list-decimal gap-1 pl-5 text-sm">
          {STEPS.map((step) => (
            <li key={step}>{step}</li>
          ))}
        </ol>
      )}
    </aside>
  )
}

/**
 * A picture of the Mosaic's screen, drawn with the tokens the firmware uses.
 * Hidden from screen readers: the steps say the same thing, and the sample
 * code isn't one to type. The address and timer lines stay plain bars until
 * the device's pairing screen settles what they say.
 */
function DeviceScreen({ paired }: { paired: boolean }) {
  return (
    <div
      aria-hidden="true"
      className="border-surface-raised bg-background grid aspect-video grid-rows-[auto_1fr] rounded-lg border-4 p-[7%]"
    >
      <Wordmark className="text-[0.625rem]" />
      {paired ? (
        <div className="text-heading flex items-center gap-2 self-center">
          <CircleCheck className="text-success size-6" />
          Paired
        </div>
      ) : (
        <div className="grid gap-2 self-center">
          <span className="text-heading tracking-[0.12em]">K7QF-2M9X</span>
          <span className="bg-border h-1.5 w-[55%] rounded-full" />
          <span className="bg-border h-1.5 w-[30%] rounded-full" />
        </div>
      )}
    </div>
  )
}

export { PairingHelp }
