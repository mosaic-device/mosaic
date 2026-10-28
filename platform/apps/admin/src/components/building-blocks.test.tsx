import { render, screen } from '@testing-library/react'
import { Monitor } from 'lucide-react'
import { describe, expect, it } from 'vitest'
import { EmptyState } from '@/components/EmptyState'
import { PageLoading } from '@/components/PageLoading'
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
})
