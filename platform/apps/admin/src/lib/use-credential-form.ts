import { useRef, useState, type ChangeEvent } from 'react'
import { flushSync } from 'react-dom'
import type { FieldMessages } from '@/api/credential-rules'
import type { ApiError } from '@/api/errors'
import { useCountdown } from '@/lib/use-countdown'

/**
 * Values, field messages and the form-level error for the setup and login
 * forms. Changing a field clears its message.
 */
export function useCredentialForm<F extends string>(fields: readonly F[]) {
  const formRef = useRef<HTMLFormElement>(null)
  const [values, setValues] = useState(
    () =>
      Object.fromEntries(fields.map((field) => [field, ''])) as Record<
        F,
        string
      >,
  )
  const [errors, setErrors] = useState<FieldMessages<F>>({})
  const [notice, setNotice] = useState<ApiError | null>(null)
  const wait = useCountdown()

  function fieldProps(field: F) {
    return {
      name: field,
      value: values[field],
      error: errors[field],
      onChange: (event: ChangeEvent<HTMLInputElement>) => {
        const { value } = event.target
        setValues((current) => ({ ...current, [field]: value }))
        setErrors((current) => {
          if (current[field] === undefined) return current
          const next = { ...current }
          delete next[field]
          return next
        })
      },
    }
  }

  function clearMessages() {
    setErrors({})
    setNotice(null)
  }

  /** Shows field messages and moves focus to the first field with one. */
  function showFieldErrors(next: FieldMessages<F>) {
    flushSync(() => setErrors(next))
    const first = fields.find((field) => next[field] !== undefined)
    const input = first && formRef.current?.elements.namedItem(first)
    if (input instanceof HTMLInputElement) input.focus()
  }

  /**
   * Field messages from `validation_failed` go under their fields. Anything
   * else is shown above the form, and `too_many_requests` starts the wait.
   */
  function showError(error: ApiError) {
    if (error.code === 'validation_failed') {
      const fromServer: FieldMessages<F> = {}
      for (const field of fields) {
        const messages = error.fieldErrors[field]
        if (messages?.length) fromServer[field] = messages.join(' ')
      }
      if (Object.keys(fromServer).length > 0) {
        showFieldErrors(fromServer)
        return
      }
    }
    if (error.code === 'too_many_requests' && error.retryAfterSeconds) {
      wait.start(error.retryAfterSeconds)
    }
    setNotice(error)
  }

  return {
    formRef,
    values,
    setValues,
    notice,
    secondsLeft: wait.secondsLeft,
    fieldProps,
    clearMessages,
    showFieldErrors,
    showError,
  }
}

export function hasMessages(messages: object): boolean {
  return Object.keys(messages).length > 0
}

/** The submit button's label while a `Retry-After` wait runs. */
export function waitLabel(secondsLeft: number): string {
  return `Try again in ${secondsLeft} ${secondsLeft === 1 ? 'second' : 'seconds'}`
}
