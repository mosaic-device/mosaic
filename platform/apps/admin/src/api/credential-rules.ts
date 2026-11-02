import type { LoginInput, SetupInput } from '@/api/auth'

// Mirrors the server's field rules and messages (`CredentialRules.cs`) so a
// form can point at a mistake before sending. The server stays the authority.

export type FieldMessages<F extends string> = Partial<Record<F, string>>

const required = 'Required.'

export function validateSetup(
  input: SetupInput,
): FieldMessages<keyof SetupInput> {
  return messages({
    username: usernameError(input.username),
    password: passwordError(input.password),
    setupCode: input.setupCode.trim() === '' ? required : undefined,
  })
}

/** Login only checks for empty fields, like the server, so it never reveals the password rules. */
export function validateLogin(
  input: LoginInput,
): FieldMessages<keyof LoginInput> {
  return messages({
    username: input.username === '' ? required : undefined,
    password: input.password === '' ? required : undefined,
  })
}

function usernameError(username: string): string | undefined {
  if (username === '') return required
  if (username.length < 3 || username.length > 32) {
    return 'Must be 3 to 32 characters.'
  }
  if (!/^[A-Za-z0-9._-]+$/.test(username)) {
    return "Use only letters, digits, '.', '_' or '-'."
  }
  return undefined
}

function passwordError(password: string): string | undefined {
  if (password === '') return required
  if (password.length < 12) return 'Must be at least 12 characters.'
  if (password.length > 128) return 'Must be at most 128 characters.'
  return undefined
}

function messages<F extends string>(
  all: Record<F, string | undefined>,
): FieldMessages<F> {
  const result: FieldMessages<F> = {}
  for (const field of Object.keys(all) as F[]) {
    const message = all[field]
    if (message !== undefined) result[field] = message
  }
  return result
}
