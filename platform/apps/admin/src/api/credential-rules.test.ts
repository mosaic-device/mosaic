import { describe, expect, it } from 'vitest'
import { validateLogin, validateSetup } from '@/api/credential-rules'

const valid = {
  username: 'bridger',
  password: 'a-long-password',
  setupCode: 'k7qf2m9x',
}

describe('validateSetup', () => {
  it('accepts valid fields', () => {
    expect(validateSetup(valid)).toEqual({})
  })

  it('requires every field', () => {
    expect(
      validateSetup({ username: '', password: '', setupCode: '  ' }),
    ).toEqual({
      username: 'Required.',
      password: 'Required.',
      setupCode: 'Required.',
    })
  })

  it.each([
    ['ab', 'Must be 3 to 32 characters.'],
    ['a'.repeat(33), 'Must be 3 to 32 characters.'],
    ['bridger davidson', "Use only letters, digits, '.', '_' or '-'."],
    ['brídger', "Use only letters, digits, '.', '_' or '-'."],
  ])('rejects the username %j', (username, message) => {
    expect(validateSetup({ ...valid, username })).toEqual({ username: message })
  })

  it('accepts usernames at the limits with every allowed character', () => {
    expect(validateSetup({ ...valid, username: 'a._' })).toEqual({})
    expect(
      validateSetup({ ...valid, username: 'A-9'.repeat(10) + 'zz' }),
    ).toEqual({})
  })

  it.each([
    ['a'.repeat(11), 'Must be at least 12 characters.'],
    ['a'.repeat(129), 'Must be at most 128 characters.'],
  ])('rejects a password of %#', (password, message) => {
    expect(validateSetup({ ...valid, password })).toEqual({ password: message })
  })

  it('accepts passwords of 12 and 128 characters', () => {
    expect(validateSetup({ ...valid, password: 'a'.repeat(12) })).toEqual({})
    expect(validateSetup({ ...valid, password: 'a'.repeat(128) })).toEqual({})
  })
})

describe('validateLogin', () => {
  it('only requires both fields, so it never reveals the password rules', () => {
    expect(validateLogin({ username: 'x', password: 'short' })).toEqual({})
    expect(validateLogin({ username: '', password: '' })).toEqual({
      username: 'Required.',
      password: 'Required.',
    })
  })
})
