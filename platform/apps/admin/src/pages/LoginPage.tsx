import { getRouteApi, useNavigate } from '@tanstack/react-router'
import { Info } from 'lucide-react'
import type { FormEvent } from 'react'
import { login } from '@/api/auth'
import { validateLogin } from '@/api/credential-rules'
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

const route = getRouteApi('/public/login')
const fields = ['username', 'password'] as const

function LoginPage() {
  const { setup } = route.useSearch()
  const navigate = useNavigate()
  const { run, pending } = useApiAction(login)
  const form = useCredentialForm(fields)

  async function handleSubmit(event: FormEvent<HTMLFormElement>) {
    event.preventDefault()
    const input = { ...form.values, username: form.values.username.trim() }
    form.clearMessages()
    const problems = validateLogin(input)
    if (hasMessages(problems)) {
      form.showFieldErrors(problems)
      return
    }

    const result = await run(input)
    if (result.status === 'cancelled') return
    if (result.status === 'success') {
      await navigate({ to: '/', replace: true })
      return
    }
    const { error } = result
    if (error.code === 'setup_required') {
      await navigate({ to: '/setup', replace: true })
      return
    }
    if (error.code === 'invalid_credentials') {
      form.setValues((current) => ({ ...current, password: '' }))
      const password = form.formRef.current?.elements.namedItem('password')
      if (password instanceof HTMLInputElement) password.focus()
    }
    form.showError(error)
  }

  return (
    <AuthCard
      title="Log in"
      description="Use the admin account for this server."
    >
      <form
        ref={form.formRef}
        noValidate
        className="grid gap-5"
        onSubmit={(event) => void handleSubmit(event)}
      >
        {setup === 'complete' && (
          <p className="border-border bg-surface-raised flex gap-3 rounded-md border px-4 py-3">
            <Info
              aria-hidden="true"
              className="text-info mt-0.5 size-5 shrink-0"
            />
            This server is already set up. Log in with its admin account.
          </p>
        )}
        <ApiErrorNotice error={form.notice} />
        <TextField
          id="login-username"
          label="Username"
          autoComplete="username"
          autoCapitalize="none"
          spellCheck={false}
          required
          {...form.fieldProps('username')}
        />
        <TextField
          id="login-password"
          label="Password"
          type="password"
          autoComplete="current-password"
          required
          {...form.fieldProps('password')}
        />
        <Button
          type="submit"
          className="w-full"
          pending={pending}
          disabled={form.secondsLeft > 0}
        >
          {pending
            ? 'Logging in…'
            : form.secondsLeft > 0
              ? waitLabel(form.secondsLeft)
              : 'Log in'}
        </Button>
      </form>
    </AuthCard>
  )
}

export { LoginPage }
