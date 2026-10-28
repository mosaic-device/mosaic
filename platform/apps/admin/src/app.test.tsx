import { screen } from '@testing-library/react'
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
