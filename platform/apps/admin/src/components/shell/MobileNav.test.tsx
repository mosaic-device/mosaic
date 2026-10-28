import { fireEvent, screen, waitFor, within } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { renderAppAt } from '@/test/render-app'

describe('phone menu', () => {
  it('opens with the main links', async () => {
    await renderAppAt('/')
    fireEvent.click(screen.getByRole('button', { name: 'Open menu' }))
    const dialog = await screen.findByRole('dialog', { name: 'Menu' })
    expect(within(dialog).getByRole('link', { name: 'Devices' })).toBeTruthy()
  })

  it('navigates and closes when a link is chosen', async () => {
    await renderAppAt('/')
    fireEvent.click(screen.getByRole('button', { name: 'Open menu' }))
    const dialog = await screen.findByRole('dialog', { name: 'Menu' })
    fireEvent.click(within(dialog).getByRole('link', { name: 'Settings' }))
    expect(
      await screen.findByRole('heading', { level: 1, name: 'Settings' }),
    ).toBeTruthy()
    await waitFor(() => expect(screen.queryByRole('dialog')).toBeNull())
  })

  it('closes on Escape and returns focus to the menu button', async () => {
    await renderAppAt('/')
    const button = screen.getByRole('button', { name: 'Open menu' })
    fireEvent.click(button)
    await screen.findByRole('dialog', { name: 'Menu' })
    fireEvent.keyDown(document.activeElement ?? document.body, {
      key: 'Escape',
    })
    await waitFor(() => expect(screen.queryByRole('dialog')).toBeNull())
    expect(document.activeElement).toBe(button)
  })
})
