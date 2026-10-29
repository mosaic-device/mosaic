import { screen, within } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { renderAppAt } from '@/test/render-app'

describe('page content', () => {
  it('Home shows every connection as not set up, with no actions', async () => {
    await renderAppAt('/')
    const main = screen.getByRole('main')
    expect(within(main).getByText('Not paired')).toBeTruthy()
    expect(within(main).getAllByText('Not connected')).toHaveLength(2)
    expect(
      within(main).getByRole('heading', {
        level: 3,
        name: 'No notifications yet',
      }),
    ).toBeTruthy()
    expect(within(main).queryAllByRole('button')).toHaveLength(0)
  })

  it('Devices shows the empty state without a pair action', async () => {
    await renderAppAt('/devices')
    const main = screen.getByRole('main')
    expect(
      within(main).getByRole('heading', { level: 2, name: 'No devices yet' }),
    ).toBeTruthy()
    expect(within(main).queryAllByRole('button')).toHaveLength(0)
  })

  it('Settings shows its empty state', async () => {
    await renderAppAt('/settings')
    expect(
      screen.getByRole('heading', { level: 2, name: 'Nothing to set up yet' }),
    ).toBeTruthy()
  })

  it('Not found links back to Home', async () => {
    await renderAppAt('/no-such-page')
    expect(
      screen.getByRole('link', { name: 'Go to Home' }).getAttribute('href'),
    ).toBe('/')
  })
})
