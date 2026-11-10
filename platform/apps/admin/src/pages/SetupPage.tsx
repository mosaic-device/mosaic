import { useNavigate } from '@tanstack/react-router'
import type { FormEvent } from 'react'
import { completeSetup } from '@/api/auth'
import { validateSetup } from '@/api/credential-rules'
import { useApiAction } from '@/api/use-api-action'
import { ApiErrorNotice } from '@/components/ApiErrorNotice'
import { AuthCard } from '@/components/AuthCard'
import { TextField } from '@/components/TextField'
import { Button } from '@/components/ui/button'
import {
  hasMessages,
  useCredentialForm,
  waitLabel,
} from '@/lib/use-credential-form'

const fields = ['username', 'password', 'setupCode'] as const

function SetupPage() {
  const navigate = useNavigate()
  const { run, pending } = useApiAction(completeSetup)
  const {
    clearMessages,
    fieldProps,
    formRef,
    notice,
    readValues,
    secondsLeft,
    showError,
    showFieldErrors,
  } = useCredentialForm(fields)

  async function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault()
    // Usernames never contain spaces, so a stray one is only a typo.
    const values = readValues()
    const input = { ...values, username: values.username.trim() }
    clearMessages()
    const problems = validateSetup(input)
    if (hasMessages(problems)) {
      showFieldErrors(problems)
      return
    }

    const result = await run(input)
    if (result.status === 'cancelled') return
    if (result.status === 'success') {
      await navigate({ to: '/', replace: true })
      return
    }
    const { error } = result
    if (error.code === 'setup_already_completed') {
      await navigate({
        to: '/login',
        search: { setup: 'complete' },
        replace: true,
      })
    } else if (error.code === 'invalid_setup_code') {
      showFieldErrors({ setupCode: error.message })
    } else {
      showError(error)
    }
  }

  return (
    <AuthCard
      title="Set up Mosaic"
      description="Create the admin account for this server. There is only one, and it can't be reset."
      documentTitle="Set up"
    >
      <form
        ref={formRef}
        noValidate
        className="grid gap-5"
        onSubmit={(event) => void handleSubmit(event)}
      >
        <ApiErrorNotice error={notice} />
        <TextField
          id="setup-username"
          label="Username"
          hint="3 to 32 letters, digits, '.', '_' or '-'."
          autoComplete="username"
          autoCapitalize="none"
          spellCheck={false}
          autoCorrect="off"
          required
          {...fieldProps('username')}
        />
        <TextField
          id="setup-password"
          label="Password"
          type="password"
          hint="At least 12 characters."
          autoComplete="new-password"
          required
          {...fieldProps('password')}
        />
        <TextField
          id="setup-code"
          label="Setup code"
          hint='The server prints it when it starts. Look for "Setup code" in its console.'
          autoComplete="off"
          autoCapitalize="characters"
          spellCheck={false}
          autoCorrect="off"
          required
          {...fieldProps('setupCode')}
        />
        <Button
          type="submit"
          className="w-full"
          pending={pending}
          disabled={secondsLeft > 0}
        >
          {pending
            ? 'Creating account…'
            : secondsLeft > 0
              ? waitLabel(secondsLeft)
              : 'Create account'}
        </Button>
      </form>
    </AuthCard>
  )
}

export { SetupPage }
