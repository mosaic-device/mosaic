import { useState } from 'react'
import { Settings, Check, Hourglass, Monitor } from 'lucide-react'
import { ApiError } from '@/api/errors'
import { ApiErrorNotice } from '@/components/ApiErrorNotice'
import { EmptyState } from '@/components/EmptyState'
import { PageLoading } from '@/components/PageLoading'
import { StatusIndicator } from '@/components/StatusIndicator'
import { Button } from '@/components/ui/button'
import {
  Card,
  CardHeader,
  CardTitle,
  CardDescription,
  CardContent,
} from '@/components/ui/card'
import { Label } from '@/components/ui/label'
import { Input } from '@/components/ui/input'
import { Checkbox } from '@/components/ui/checkbox'
import { Switch } from '@/components/ui/switch'
import { Separator } from '@/components/ui/separator'
import {
  Dialog,
  DialogTrigger,
  DialogContent,
  DialogHeader,
  DialogTitle,
  DialogDescription,
  DialogFooter,
  DialogClose,
} from '@/components/ui/dialog'

const stack = 'grid min-w-0 gap-[var(--space-4)]'
const controlRow = 'flex flex-wrap items-center gap-[var(--space-3)]'
const choice =
  'flex min-h-[var(--control-touch-target)] items-center gap-[var(--space-3)]'

const sampleErrors = [
  new ApiError({
    kind: 'problem',
    status: 500,
    code: 'internal_error',
    title: 'Something went wrong.',
    traceId: '00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01',
  }),
  new ApiError({ kind: 'unavailable', status: 502 }),
  new ApiError({ kind: 'malformed', status: 200 }),
]

export default function ComponentPreview() {
  const [selected, setSelected] = useState('First example')
  return (
    <main className="m-auto grid max-w-[1120px] gap-[var(--space-8)] p-[var(--space-8)] [@media(max-width:640px)]:p-[var(--space-5)]">
      <header className={stack}>
        {/* The shared font allowlist omits the configured wordmark-only brand family. */}
        {/* eslint-disable-next-line no-restricted-syntax */}
        <p className="font-brand font-medium tracking-[0.23em]">MOSAIC</p>
        <p className="text-caption text-muted-foreground">
          Development only · Phase 3A
        </p>
        <h1>Component foundation</h1>
        <p className="text-[color:var(--muted-foreground)]">
          Dark appearance. Shared tokens, self-hosted type, and accessible
          controls. No product behavior or backend requests.
        </p>
      </header>
      <Separator />
      <div className="grid grid-cols-2 gap-[var(--space-6)] [@media(max-width:640px)]:grid-cols-[1fr]">
        <Card>
          <CardHeader>
            <CardTitle>Typography</CardTitle>
            <CardDescription>
              Manrope for the interface. Outfit 500 for the wordmark only.
            </CardDescription>
          </CardHeader>
          <CardContent className={stack}>
            <p className="text-title">Title · Manrope 500</p>
            <p className="text-heading">Heading · Manrope 500</p>
            <p>
              Body · 400. A longer description should wrap naturally without
              hiding important information on a narrow screen.
            </p>
            <Label>Label · 500</Label>
            <p className="text-caption text-muted-foreground">
              Caption · 12px / 400
            </p>
            <p>09:41 · 0123456789 · 23°C · café</p>
          </CardContent>
        </Card>
        <Card>
          <CardHeader>
            <CardTitle>Semantic palette</CardTitle>
            <CardDescription>
              Status is not an alternate brand accent.
            </CardDescription>
          </CardHeader>
          <CardContent className="grid grid-cols-4 gap-[var(--space-3)]">
            {[
              'background',
              'surface',
              'surface-raised',
              'primary',
              'info',
              'success',
              'warning',
              'danger',
            ].map((role) => (
              <div key={role}>
                <div
                  className="mb-[var(--space-2)] h-[var(--control-lg)] rounded-[var(--radius-sm)] border border-[var(--color-interactive-border)]"
                  style={{ background: `var(--color-${role})` }}
                />
                <p className="text-caption text-muted-foreground">{role}</p>
              </div>
            ))}
          </CardContent>
        </Card>
        <Card>
          <CardHeader>
            <CardTitle>Actions</CardTitle>
            <CardDescription>
              Tab for focus. Hold a pointer down to inspect pressed states.
            </CardDescription>
          </CardHeader>
          <CardContent className={stack}>
            <div className={controlRow}>
              <Button>Primary action</Button>
              <Button variant="secondary">Secondary action</Button>
              <Button variant="ghost">Ghost action</Button>
              <Button variant="destructive">Destructive action</Button>
              <Button size="icon" variant="ghost" aria-label="Settings">
                <Settings aria-hidden="true" />
              </Button>
            </div>
            <div className={controlRow}>
              <Button disabled>Disabled action</Button>
              <Button pending>
                <Hourglass aria-hidden="true" />
                Saving…
              </Button>
            </div>
            <Dialog>
              <DialogTrigger asChild>
                <Button variant="secondary">Open example dialog</Button>
              </DialogTrigger>
              <DialogContent>
                <DialogHeader>
                  <DialogTitle>Example dialog</DialogTitle>
                  <DialogDescription>
                    Focus stays inside this dialog. Escape closes it and returns
                    focus to the trigger. Nothing is saved.
                  </DialogDescription>
                </DialogHeader>
                <div className={stack}>
                  <Label htmlFor="dialog-name">Example name</Label>
                  <Input id="dialog-name" defaultValue="Preview only" />
                </div>
                <DialogFooter>
                  <DialogClose asChild>
                    <Button variant="secondary">Cancel</Button>
                  </DialogClose>
                  <DialogClose asChild>
                    <Button>Done</Button>
                  </DialogClose>
                </DialogFooter>
              </DialogContent>
            </Dialog>
          </CardContent>
        </Card>
        <Card>
          <CardHeader>
            <CardTitle>Form fields</CardTitle>
            <CardDescription>
              Labels and errors remain associated with their field.
            </CardDescription>
          </CardHeader>
          <CardContent>
            <form
              className={stack}
              onSubmit={(event) => event.preventDefault()}
            >
              <div className={stack}>
                <Label htmlFor="example-name">Example name</Label>
                <Input
                  id="example-name"
                  name="name"
                  placeholder="Enter a name"
                  autoComplete="off"
                />
              </div>
              <div className={stack}>
                <Label htmlFor="example-error">Required value</Label>
                <Input
                  id="example-error"
                  name="requiredValue"
                  required
                  aria-invalid="true"
                  aria-describedby="example-error-message"
                  defaultValue=""
                />
                <p
                  id="example-error-message"
                  className="text-[length:var(--typography-label)] text-[color:var(--color-danger)]"
                >
                  Enter a value before continuing.
                </p>
              </div>
              <div className={stack}>
                <Label htmlFor="example-disabled">Disabled field</Label>
                <Input
                  id="example-disabled"
                  disabled
                  value="Unavailable in this example"
                />
              </div>
              <div className={choice}>
                <Checkbox id="example-checkbox" name="checked" defaultChecked />
                <Label htmlFor="example-checkbox">Include this example</Label>
              </div>
              <div className={choice}>
                <Switch id="example-switch" name="enabled" />
                <Label htmlFor="example-switch">Enable this example</Label>
              </div>
              <div className={choice}>
                <Switch id="example-switch-small" name="compact" size="sm" />
                <Label htmlFor="example-switch-small">
                  Compact switch example
                </Label>
              </div>
              <div className={choice}>
                <Checkbox id="disabled-checkbox" disabled />
                <Label htmlFor="disabled-checkbox">Unavailable option</Label>
              </div>
            </form>
          </CardContent>
        </Card>
        <Card>
          <CardHeader>
            <CardTitle>Selection and focus</CardTitle>
            <CardDescription>
              Selection persists when keyboard focus moves elsewhere.
            </CardDescription>
          </CardHeader>
          <CardContent className={stack}>
            {[
              'First example',
              'A long example label that wraps instead of truncating useful information on a phone',
            ].map((name) => (
              <Button
                key={name}
                variant="ghost"
                className="flex w-full gap-[var(--space-3)] rounded-[var(--radius-md)] border border-[var(--color-interactive-border)] p-[var(--space-4)] text-left [overflow-wrap:anywhere] aria-pressed:bg-[var(--color-selected)] aria-pressed:text-[var(--color-selected-foreground)] aria-pressed:shadow-[inset_0_0_0_1px_var(--color-selected-border)] forced-colors:border-[ButtonText] forced-colors:aria-pressed:outline-2 forced-colors:aria-pressed:outline-offset-[-3px] forced-colors:aria-pressed:outline-[Highlight]"
                aria-pressed={selected === name}
                onClick={() => setSelected(name)}
              >
                {selected === name && <Check aria-hidden="true" />}
                <span>{name}</span>
              </Button>
            ))}
          </CardContent>
        </Card>
        <Card>
          <CardHeader>
            <CardTitle>Status and empty state</CardTitle>
            <CardDescription>
              Meaning is expressed in text, not color alone.
            </CardDescription>
          </CardHeader>
          <CardContent className={stack}>
            <p role="status" style={{ color: 'var(--color-success)' }}>
              Ready · Example assets loaded locally
            </p>
            <div
              className={`${stack} rounded-[var(--radius-md)] border border-dashed border-[var(--color-interactive-border)] p-[var(--space-8)] text-center`}
            >
              <h3>No examples added</h3>
              <p className="text-[color:var(--muted-foreground)]">
                An empty state explains what belongs here, without implying live
                data.
              </p>
            </div>
          </CardContent>
        </Card>
        <Card className="col-[1/-1]">
          <CardHeader>
            <CardTitle>Page patterns</CardTitle>
            <CardDescription>
              Shared states every admin page reuses.
            </CardDescription>
          </CardHeader>
          <CardContent className={stack}>
            <div className={controlRow}>
              <StatusIndicator tone="neutral">Not paired</StatusIndicator>
              <StatusIndicator tone="success">Online</StatusIndicator>
              <StatusIndicator tone="warning">Sign in again</StatusIndicator>
              <StatusIndicator tone="danger">Offline</StatusIndicator>
            </div>
            <EmptyState
              icon={Monitor}
              title="No devices yet"
              description="When you pair a Mosaic, it shows up here with whether it's online."
              headingLevel={3}
            />
            <PageLoading />
            {sampleErrors.map((error) => (
              <ApiErrorNotice key={error.kind} error={error} />
            ))}
          </CardContent>
        </Card>
      </div>
    </main>
  )
}
