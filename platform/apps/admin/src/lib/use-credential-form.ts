import { useRef, useState } from 'react'
import { flushSync } from 'react-dom'
import type { FieldMessages } from '@/api/credential-rules'
import type { ApiError } from '@/api/errors'
import { useCountdown } from '@/lib/use-countdown'

/**
 * Field messages and the form-level error for the setup and login forms.
 * Changing a field clears its message. Inputs are uncontrolled, and values
 * are read from the form on submit, so a password manager that fills a field
 * without an input event still counts.
 */
export function useCredentialForm<F extends string>(fields: readonly F[]) {
  const formRef = useRef<HTMLFormElement>(null)
  const [errors, setErrors] = useState<FieldMessages<F>>({})
  const [notice, setNotice] = useState<ApiError | null>(null)
  const wait = useCountdown()

  function inputFor(field: F): HTMLInputElement | null {
    const element = formRef.current?.elements.namedItem(field)
    return element instanceof HTMLInputElement ? element : null
  }

  /** The fields as the browser has them right now. */
  function readValues(): Record<F, string> {
    return Object.fromEntries(
      fields.map((field) => [field, inputFor(field)?.value ?? '']),
    ) as Record<F, string>
  }

  function fieldProps(field: F) {
    return {
      name: field,
      error: errors[field],
      onChange: () => {
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

  /** Empties a field and moves focus to it. */
  function clearField(field: F) {
    const input = inputFor(field)
    if (!input) return
    input.value = ''
    input.focus()
  }

  /** Shows field messages and moves focus to the first field with one. */
  function showFieldErrors(next: FieldMessages<F>) {
    flushSync(() => setErrors(next))
    const first = fields.find((field) => next[field] !== undefined)
    if (first) inputFor(first)?.focus()
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
    notice,
    secondsLeft: wait.secondsLeft,
    readValues,
    fieldProps,
    clearMessages,
    clearField,
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
