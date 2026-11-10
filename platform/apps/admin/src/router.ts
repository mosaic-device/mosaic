import {
  createRootRouteWithContext,
  createRoute,
  createRouter,
  Outlet,
  redirect,
  type RouterHistory,
} from '@tanstack/react-router'
import { getSession, getSetupStatus } from '@/api/auth'
import { isUnauthenticated } from '@/api/errors'
import { PageError } from '@/components/PageError'
import { PageLoading } from '@/components/PageLoading'
import { PublicPageLoading } from '@/components/PublicPageLoading'
import { AppShell } from '@/components/shell/AppShell'
import { PublicLayout } from '@/components/shell/PublicLayout'
import {
  SessionCheckError,
  SessionCheckLoading,
} from '@/components/shell/SessionCheck'
import { DevPreview } from '@/DevPreview'
import { returnPathFor, safeReturnPath } from '@/lib/return-path'
import { DevicesPage } from '@/pages/DevicesPage'
import { HomePage } from '@/pages/HomePage'
import { LoginPage } from '@/pages/LoginPage'
import { NotFoundPage } from '@/pages/NotFoundPage'
import { SettingsPage } from '@/pages/SettingsPage'
import { SetupPage } from '@/pages/SetupPage'

/**
 * Whether this tab has been signed in, so login can say the session ended
 * instead of looking like a first visit. Logout clears it.
 */
class SignInMemory {
  #wasSignedIn = false

  get wasSignedIn() {
    return this.#wasSignedIn
  }

  signedIn() {
    this.#wasSignedIn = true
  }

  signedOut() {
    this.#wasSignedIn = false
  }
}

type RouterContext = { auth: SignInMemory }

/**
 * - `setup: 'complete'`: setup was tried after it was already done.
 * - `redirect`: the page to open after login, always a path inside the app.
 * - `session: 'ended'`: this tab was signed in and the session is gone.
 */
type LoginSearch = { setup?: 'complete'; redirect?: string; session?: 'ended' }

const rootRoute = createRootRouteWithContext<RouterContext>()({
  component: Outlet,
})

// Setup and login render here, outside the signed-in shell. Each asks the
// server whether an admin exists and sends the visitor to the right one.
const publicRoute = createRoute({
  getParentRoute: () => rootRoute,
  id: 'public',
  component: PublicLayout,
})
const setupRoute = createRoute({
  getParentRoute: () => publicRoute,
  path: 'setup',
  loader: async ({ abortController }) => {
    const { setupRequired } = await getSetupStatus(abortController.signal)
    if (!setupRequired) {
      throw redirect({
        to: '/login',
        search: { setup: 'complete' },
        replace: true,
      })
    }
  },
  pendingComponent: PublicPageLoading,
  component: SetupPage,
})
const loginRoute = createRoute({
  getParentRoute: () => publicRoute,
  path: 'login',
  // Every key is set, even to undefined: the root route passes the raw search
  // through, so a key left out would keep its unchecked value.
  validateSearch: (search: Record<string, unknown>): LoginSearch => ({
    setup: search.setup === 'complete' ? 'complete' : undefined,
    redirect: safeReturnPath(search.redirect),
    session: search.session === 'ended' ? 'ended' : undefined,
  }),
  loaderDeps: ({ search }) => ({ returnPath: search.redirect }),
  loader: async ({ abortController, deps }) => {
    const { setupRequired } = await getSetupStatus(abortController.signal)
    if (setupRequired) throw redirect({ to: '/setup', replace: true })
    // Someone already signed in skips the form.
    try {
      await getSession(abortController.signal)
    } catch (error) {
      if (isUnauthenticated(error)) return
      throw error
    }
    throw redirect({ href: deps.returnPath ?? '/', replace: true })
  },
  pendingComponent: PublicPageLoading,
  component: LoginPage,
})

const appRoute = createRoute({
  getParentRoute: () => rootRoute,
  id: 'app',
  // Asks on every navigation: the session cookie is HttpOnly, so the server
  // is the only one who knows whether it still works.
  beforeLoad: async ({ context, location, abortController }) => {
    try {
      const session = await getSession(abortController.signal)
      context.auth.signedIn()
      return { session }
    } catch (error) {
      if (!isUnauthenticated(error)) throw error
      const search: LoginSearch = {}
      const returnPath = returnPathFor(location.href)
      if (returnPath) search.redirect = returnPath
      if (context.auth.wasSignedIn) search.session = 'ended'
      context.auth.signedOut()
      throw redirect({ to: '/login', search })
    }
  },
  pendingComponent: SessionCheckLoading,
  errorComponent: SessionCheckError,
  component: AppShell,
})
const homeRoute = createRoute({
  getParentRoute: () => appRoute,
  path: '/',
  component: HomePage,
})
const devicesRoute = createRoute({
  getParentRoute: () => appRoute,
  path: 'devices',
  component: DevicesPage,
})
const settingsRoute = createRoute({
  getParentRoute: () => appRoute,
  path: 'settings',
  component: SettingsPage,
})
const notFoundRoute = createRoute({
  getParentRoute: () => appRoute,
  path: '$',
  component: NotFoundPage,
})

const previewRoutes = import.meta.env.DEV
  ? [
      createRoute({
        getParentRoute: () => rootRoute,
        path: '/dev/components',
        component: DevPreview,
      }),
    ]
  : []

const routeTree = rootRoute.addChildren([
  publicRoute.addChildren([setupRoute, loginRoute]),
  appRoute.addChildren([homeRoute, devicesRoute, settingsRoute, notFoundRoute]),
  ...previewRoutes,
])

function createAppRouter(options: { history?: RouterHistory } = {}) {
  return createRouter({
    routeTree,
    history: options.history,
    context: { auth: new SignInMemory() },
    defaultPendingComponent: PageLoading,
    defaultErrorComponent: PageError,
  })
}

type AppRouter = ReturnType<typeof createAppRouter>

declare module '@tanstack/react-router' {
  interface Register {
    router: AppRouter
  }
}

export { createAppRouter, type AppRouter }
