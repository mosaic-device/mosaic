import { render, screen, within } from '@testing-library/react'
import { Monitor } from 'lucide-react'
import { describe, expect, it } from 'vitest'
import { EmptyState } from '@/components/EmptyState'
import { PageLoading } from '@/components/PageLoading'
import { PublicPageLoading } from '@/components/PublicPageLoading'
import { StatusIndicator } from '@/components/StatusIndicator'
import { createAppRouter } from '@/router'

describe('page building blocks', () => {
  it('PageLoading announces that the page is loading', () => {
    render(<PageLoading />)
    expect(screen.getByRole('status').textContent).toBe('Loading…')
  })

  it('StatusIndicator carries its meaning in text', () => {
    render(<StatusIndicator tone="neutral">Not paired</StatusIndicator>)
    expect(screen.getByText('Not paired')).toBeTruthy()
  })

  it('EmptyState uses the requested heading level', () => {
    render(
      <EmptyState
        icon={Monitor}
        title="No devices yet"
        description="x"
        headingLevel={3}
      />,
    )
    expect(
      screen.getByRole('heading', { level: 3, name: 'No devices yet' }),
    ).toBeTruthy()
  })

  it('the router shows PageLoading while a route is pending', () => {
    expect(createAppRouter().options.defaultPendingComponent).toBe(PageLoading)
  })

  it('PublicPageLoading is only a spinner that announces loading', () => {
    const { container } = render(<PublicPageLoading />)
    const status = screen.getByRole('status')
    expect(status.textContent).toBe('Loading…')
    expect(status.querySelector('svg')?.getAttribute('aria-hidden')).toBe(
      'true',
    )
    expect(container.querySelectorAll('div')).toHaveLength(1)
  })

  it('the signed-in area waits on a centered spinner, not the shell', () => {
    const Pending =
      createAppRouter().routesById['/app'].options.pendingComponent!
    render(<Pending />)
    const main = screen.getByRole('main')
    expect(within(main).getByRole('status').textContent).toBe('Loading…')
    expect(screen.queryByRole('navigation')).toBeNull()
  })

  it('setup and login show the spinner instead of the page skeleton', () => {
    const { routesById } = createAppRouter()
    expect(routesById['/public/setup'].options.pendingComponent).toBe(
      PublicPageLoading,
    )
    expect(routesById['/public/login'].options.pendingComponent).toBe(
      PublicPageLoading,
    )
  })
})
