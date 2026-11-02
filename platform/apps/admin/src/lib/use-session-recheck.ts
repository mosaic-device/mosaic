import { useRouter } from '@tanstack/react-router'
import { useEffect } from 'react'
import { getSession } from '@/api/auth'
import { isUnauthenticated } from '@/api/errors'

/**
 * Asks the server again when the owner comes back to the tab, so a session
 * that ended while the tab was hidden goes to login. Only a 401 acts, by
 * re-running the route guard. A failed request is ignored: it often means a
 * laptop woke before its network did, and the next page change asks again.
 */
export function useSessionRecheck(): void {
  const router = useRouter()

  useEffect(() => {
    const controller = new AbortController()
    function recheck() {
      if (document.visibilityState !== 'visible') return
      getSession(controller.signal).catch((error: unknown) => {
        if (isUnauthenticated(error)) void router.invalidate()
      })
    }
    document.addEventListener('visibilitychange', recheck)
    return () => {
      document.removeEventListener('visibilitychange', recheck)
      controller.abort()
    }
  }, [router])
}
