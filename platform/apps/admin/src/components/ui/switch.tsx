import * as React from 'react'
import { cn } from 'cn'
import { Switch as SwitchPrimitive } from 'radix-ui'

const sizes = {
  default: {
    track: 'h-(--space-6) w-(--control-md)',
    thumb: 'size-(--space-4) data-[state=checked]:translate-x-(--space-4)',
  },
  sm: {
    track: 'h-(--space-5) w-(--control-sm)',
    thumb: 'size-(--space-3) data-[state=checked]:translate-x-(--space-3)',
  },
}

function Switch({
  className,
  size = 'default',
  ...props
}: React.ComponentProps<typeof SwitchPrimitive.Root> & {
  size?: 'sm' | 'default'
}) {
  return (
    <SwitchPrimitive.Root
      data-slot="switch"
      data-size={size}
      className={cn(
        'peer group/switch border-interactive-border box-border inline-flex shrink-0 items-center rounded-full border-(length:--border-width-default) border-solid p-[calc(var(--space-1)-var(--border-width-default))] shadow-sm transition-colors',
        'data-[state=checked]:bg-primary data-[state=checked]:text-primary-foreground data-[state=unchecked]:bg-input dark:data-[state=unchecked]:bg-input/80',
        'disabled:pointer-events-none disabled:cursor-not-allowed disabled:opacity-50 forced-colors:border forced-colors:border-[ButtonText]',
        sizes[size].track,
        className,
      )}
      {...props}
    >
      <SwitchPrimitive.Thumb
        data-slot="switch-thumb"
        className={cn(
          'pointer-events-none block shrink-0 translate-x-0 rounded-full bg-(--foreground) ring-0 transition-transform group-data-[state=checked]/switch:bg-(--primary-foreground)',
          sizes[size].thumb,
        )}
      />
    </SwitchPrimitive.Root>
  )
}

export { Switch }
