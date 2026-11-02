import { fireEvent, render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { TextField } from '@/components/TextField'

describe('TextField', () => {
  it('labels the input and links its hint and error', () => {
    render(
      <TextField
        id="code"
        name="code"
        label="Setup code"
        hint="Printed by the server."
        error="Required."
      />,
    )
    const input = screen.getByLabelText('Setup code')
    expect(input.getAttribute('aria-invalid')).toBe('true')
    const describedBy = input.getAttribute('aria-describedby')?.split(' ')
    expect(describedBy).toEqual(['code-hint', 'code-error'])
    expect(document.getElementById('code-hint')?.textContent).toBe(
      'Printed by the server.',
    )
    expect(document.getElementById('code-error')?.textContent).toBe('Required.')
  })

  it('is not marked invalid without an error', () => {
    render(<TextField id="name" name="name" label="Username" />)
    const input = screen.getByLabelText('Username')
    expect(input.getAttribute('aria-invalid')).toBeNull()
    expect(input.getAttribute('aria-describedby')).toBeNull()
    expect(screen.queryByRole('button')).toBeNull()
  })

  it('shows and hides a password', () => {
    render(
      <TextField
        id="password"
        name="password"
        label="Password"
        type="password"
      />,
    )
    const input = screen.getByLabelText('Password')
    const toggle = screen.getByRole('button', { name: 'Show password' })
    expect(input.getAttribute('type')).toBe('password')
    expect(toggle.getAttribute('aria-pressed')).toBe('false')
    expect(toggle.getAttribute('aria-controls')).toBe('password')

    fireEvent.click(toggle)
    expect(input.getAttribute('type')).toBe('text')
    expect(toggle.getAttribute('aria-pressed')).toBe('true')

    fireEvent.click(toggle)
    expect(input.getAttribute('type')).toBe('password')
  })
})
