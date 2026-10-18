'use client'

import * as React from 'react'
import { cn } from 'cn'
import { CheckIcon } from 'lucide-react'
import { Checkbox as CheckboxPrimitive } from 'radix-ui'

function Checkbox({
  className,
  ...props
}: React.ComponentProps<typeof CheckboxPrimitive.Root>) {
  return (
    <CheckboxPrimitive.Root
      data-slot="checkbox"
      className={cn(
        'peer border-interactive-border data-[state=checked]:bg-primary data-[state=checked]:text-primary-foreground size-6 shrink-0 rounded-[4px] border shadow-sm transition-shadow disabled:pointer-events-none disabled:cursor-not-allowed disabled:opacity-50 forced-colors:border-[ButtonText]',
        className,
      )}
      {...props}
    >
      <CheckboxPrimitive.Indicator
        data-slot="checkbox-indicator"
        className="grid place-content-center text-current transition-none"
      >
        <CheckIcon className="size-3.5" />
      </CheckboxPrimitive.Indicator>
    </CheckboxPrimitive.Root>
  )
}

export { Checkbox }
