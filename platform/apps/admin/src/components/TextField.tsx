import { useState, type ComponentProps, type ReactNode } from 'react'
import { Eye, EyeOff } from 'lucide-react'
import { Button } from '@/components/ui/button'
import { Input } from '@/components/ui/input'
import { Label } from '@/components/ui/label'

type TextFieldProps = Omit<
  ComponentProps<'input'>,
  'id' | 'aria-invalid' | 'aria-describedby'
> & {
  id: string
  label: string
  hint?: ReactNode
  error?: string
}

/**
 * A labelled input with optional hint and error text. A password field gets a
 * "Show password" toggle, since there is no reset if setup is mistyped.
 */
function TextField({
  id,
  label,
  hint,
  error,
  type = 'text',
  ...props
}: TextFieldProps) {
  const [shown, setShown] = useState(false)
  const isPassword = type === 'password'
  const hintId = hint ? `${id}-hint` : undefined
  const errorId = error ? `${id}-error` : undefined
  const describedBy = [hintId, errorId].filter(Boolean).join(' ') || undefined

  return (
    <div className="grid gap-2">
      <Label htmlFor={id}>{label}</Label>
      <div className="flex gap-2">
        <Input
          id={id}
          type={isPassword && shown ? 'text' : type}
          aria-invalid={error ? true : undefined}
          aria-describedby={describedBy}
          {...props}
        />
        {isPassword && (
          <Button
            variant="secondary"
            size="icon"
            className="shrink-0"
            aria-label="Show password"
            aria-pressed={shown}
            aria-controls={id}
            onClick={() => setShown((value) => !value)}
          >
            {shown ? <EyeOff aria-hidden="true" /> : <Eye aria-hidden="true" />}
          </Button>
        )}
      </div>
      {hint && (
        <p id={hintId} className="text-muted-foreground text-sm">
          {hint}
        </p>
      )}
      {error && (
        <p id={errorId} className="text-danger text-sm">
          {error}
        </p>
      )}
    </div>
  )
}

export { TextField }
