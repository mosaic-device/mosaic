import { fireEvent, screen, within } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { renderAppAt } from '@/test/render-app'

describe('direct load', () => {
  it.each([
    ['/', 'Welcome', 'Home · Mosaic Admin'],
    ['/devices', 'Devices', 'Devices · Mosaic Admin'],
    ['/devices/', 'Devices', 'Devices · Mosaic Admin'],
    ['/settings', 'Settings', 'Settings · Mosaic Admin'],
    ['/no-such-page', 'Page not found', 'Page not found · Mosaic Admin'],
    ['/a/b/c', 'Page not found', 'Page not found · Mosaic Admin'],
  ])('%s renders its page', async (path, heading, title) => {
    await renderAppAt(path)
    expect(
      screen.getByRole('heading', { level: 1, name: heading }),
    ).toBeTruthy()
    expect(document.title).toBe(title)
  })
})

describe('navigation', () => {
  it.each([
    ['/', 'Home'],
    ['/devices', 'Devices'],
    ['/devices/', 'Devices'],
    ['/settings', 'Settings'],
  ])('on %s only %s is current', async (path, label) => {
    await renderAppAt(path)
    const nav = screen.getByRole('navigation', { name: 'Main' })
    for (const link of within(nav).getAllByRole('link')) {
      expect(link.getAttribute('aria-current')).toBe(
        link.textContent?.trim() === label ? 'page' : null,
      )
    }
  })

  it('clicking a link changes the page and the current link', async () => {
    await renderAppAt('/')
    const nav = screen.getByRole('navigation', { name: 'Main' })
    fireEvent.click(within(nav).getByRole('link', { name: 'Devices' }))
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Devices' }),
    ).toBeTruthy()
    expect(
      within(nav)
        .getByRole('link', { name: 'Devices' })
        .getAttribute('aria-current'),
    ).toBe('page')
    expect(
      within(nav)
        .getByRole('link', { name: 'Home' })
        .getAttribute('aria-current'),
    ).toBeNull()
  })

  it('marks no link current on a not-found page under a section', async () => {
    await renderAppAt('/devices/no-such-device')
    expect(
      screen.getByRole('heading', { level: 1, name: 'Page not found' }),
    ).toBeTruthy()
    const nav = screen.getByRole('navigation', { name: 'Main' })
    for (const link of within(nav).getAllByRole('link')) {
      expect(link.getAttribute('aria-current')).toBeNull()
    }
  })

  it('keeps the navigation on the not-found page', async () => {
    await renderAppAt('/no-such-page')
    expect(screen.getByRole('navigation', { name: 'Main' })).toBeTruthy()
  })

  it('skip link targets the focusable main content', async () => {
    await renderAppAt('/')
    expect(
      screen
        .getByRole('link', { name: 'Skip to content' })
        .getAttribute('href'),
    ).toBe('#main-content')
    const main = document.getElementById('main-content')
    expect(main?.tagName).toBe('MAIN')
    expect(main?.getAttribute('tabindex')).toBe('-1')
  })
})
