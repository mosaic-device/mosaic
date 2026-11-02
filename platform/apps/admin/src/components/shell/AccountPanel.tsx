import { getRouteApi, useRouter } from '@tanstack/react-router'
import { LogOut } from 'lucide-react'
import { useState } from 'react'
import { logout } from '@/api/auth'
import { ApiErrorNotice } from '@/components/ApiErrorNotice'
import { Button } from '@/components/ui/button'

const appRoute = getRouteApi('/app')

/** Who is signed in, and the way out. Used in the sidebar and the phone menu. */
function AccountPanel() {
  const { session, auth } = appRoute.useRouteContext()
  const router = useRouter()
  const [pending, setPending] = useState(false)
  const [error, setError] = useState<unknown>(null)

  // Not `useApiAction`: closing the phone menu unmounts this panel, and the
  // server may already have ended the session, so the request must finish
  // and the app must follow it to login.
  async function handleLogout() {
    setPending(true)
    setError(null)
    // Cleared first, so a page change during logout doesn't say the session ended.
    auth.wasSignedIn = false
    try {
      await logout()
    } catch (caught) {
      // The server never heard it, so the owner is still signed in. Say so
      // rather than sending them to login.
      auth.wasSignedIn = true
      setPending(false)
      setError(caught)
      return
    }
    router.clearCache()
    // Stays pending until the login page replaces this one.
    await router.navigate({ to: '/login', replace: true })
  }

  return (
    <div className="grid gap-3">
      <p className="text-muted-foreground px-3 text-sm break-words">
        Signed in as {session.admin.username}
      </p>
      <Button
        variant="secondary"
        pending={pending}
        onClick={() => void handleLogout()}
      >
        <LogOut aria-hidden="true" />
        {pending ? 'Logging out…' : 'Log out'}
      </Button>
      <ApiErrorNotice error={error} />
    </div>
  )
}

export { AccountPanel }
