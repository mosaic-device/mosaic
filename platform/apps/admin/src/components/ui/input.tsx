import * as React from 'react'
import { cn } from 'cn'

function Input({ className, type, ...props }: React.ComponentProps<'input'>) {
  return (
    <input
      type={type}
      data-slot="input"
      className={cn(
        'border-interactive-border selection:bg-primary selection:text-primary-foreground file:text-foreground placeholder:text-muted-foreground min-h-control-md [@media(max-width:640px)]:min-h-control-touch bg-surface-sunken text-body aria-invalid:border-danger h-9 w-full min-w-0 rounded-md border px-3 py-1 shadow-sm transition-[color,box-shadow] file:inline-flex file:h-7 file:border-0 file:bg-transparent file:text-sm file:font-medium disabled:pointer-events-none disabled:cursor-not-allowed disabled:opacity-50',
        className,
      )}
      {...props}
    />
  )
}

export { Input }
