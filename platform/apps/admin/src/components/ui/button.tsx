import * as React from 'react'
import { cva, type VariantProps } from 'class-variance-authority'
import { cn } from 'cn'
import { Slot } from 'radix-ui'

const buttonVariants = cva(
  'min-h-control-md [@media(max-width:640px)]:min-h-control-touch border border-transparent whitespace-normal disabled:opacity-50 disabled:cursor-not-allowed disabled:pointer-events-none forced-colors:border-[ButtonText] inline-flex items-center justify-center gap-2 rounded-md text-sm font-medium [&_svg]:size-5',
  {
    variants: {
      variant: {
        default:
          'bg-primary text-primary-foreground hover:bg-primary-hover active:bg-primary-pressed',
        destructive:
          'bg-danger text-danger-foreground hover:bg-[color-mix(in_srgb,var(--color-danger),var(--foreground)_6%)] active:bg-[color-mix(in_srgb,var(--color-danger),var(--background)_12%)]',
        outline:
          'bg-secondary text-secondary-foreground border-interactive-border hover:bg-secondary-hover active:bg-secondary-pressed',
        secondary:
          'bg-secondary text-secondary-foreground border-interactive-border hover:bg-secondary-hover active:bg-secondary-pressed',
        ghost:
          'bg-transparent text-foreground hover:bg-secondary-hover active:bg-secondary-pressed',
        link: 'bg-transparent text-foreground hover:bg-secondary-hover active:bg-secondary-pressed underline',
      },
      size: {
        default: 'h-10 px-4 py-2',
        xs: "h-6 gap-1 rounded-md px-2 text-xs has-[>svg]:px-1.5 [&_svg:not([class*='size-'])]:size-3",
        sm: 'h-8 gap-1.5 rounded-md px-3 has-[>svg]:px-2.5',
        lg: 'h-12 rounded-md px-6 has-[>svg]:px-4',
        icon: 'size-10',
        'icon-xs': "size-6 rounded-md [&_svg:not([class*='size-'])]:size-3",
        'icon-sm': 'size-8',
        'icon-lg': 'size-10',
      },
    },
    defaultVariants: {
      variant: 'default',
      size: 'default',
    },
  },
)

function Button({
  className,
  variant = 'default',
  size = 'default',
  asChild = false,
  pending = false,
  disabled,
  type = 'button',
  ...props
}: React.ComponentProps<'button'> &
  VariantProps<typeof buttonVariants> & {
    asChild?: boolean
    pending?: boolean
  }) {
  const Comp = asChild ? Slot.Root : 'button'

  return (
    <Comp
      type={type}
      disabled={disabled || pending}
      aria-busy={pending || undefined}
      data-slot="button"
      data-variant={variant}
      data-size={size}
      className={cn(buttonVariants({ variant, size, className }))}
      {...props}
    />
  )
}

export { Button }
