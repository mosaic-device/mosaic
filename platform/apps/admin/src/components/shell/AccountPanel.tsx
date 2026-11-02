import { getRouteApi, useRouter } from '@tanstack/react-router'
import { LogOut } from 'lucide-react'
import { logout } from '@/api/auth'
import { useApiAction } from '@/api/use-api-action'
import { ApiErrorNotice } from '@/components/ApiErrorNotice'
import { Button } from '@/components/ui/button'

const appRoute = getRouteApi('/app')

/** Who is signed in, and the way out. Used in the sidebar and the phone menu. */
function AccountPanel() {
  const { session, auth } = appRoute.useRouteContext()
  const router = useRouter()
  const { run, pending, error } = useApiAction(
    (_input: void, signal: AbortSignal) => logout(signal),
  )

  // A failed logout keeps the owner signed in and says so: the server still
  // holds the session, so sending them to login would be misleading.
  async function handleLogout() {
    const result = await run()
    if (result.status !== 'success') return
    auth.wasSignedIn = false
    router.clearCache()
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
