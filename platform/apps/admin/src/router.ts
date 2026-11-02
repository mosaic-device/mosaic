import {
  createRootRoute,
  createRoute,
  createRouter,
  Outlet,
  redirect,
  type RouterHistory,
} from '@tanstack/react-router'
import { getSetupStatus } from '@/api/auth'
import { PageError } from '@/components/PageError'
import { PageLoading } from '@/components/PageLoading'
import { PublicPageLoading } from '@/components/PublicPageLoading'
import { AppShell } from '@/components/shell/AppShell'
import { PublicLayout } from '@/components/shell/PublicLayout'
import { DevPreview } from '@/DevPreview'
import { DevicesPage } from '@/pages/DevicesPage'
import { HomePage } from '@/pages/HomePage'
import { LoginPage } from '@/pages/LoginPage'
import { NotFoundPage } from '@/pages/NotFoundPage'
import { SettingsPage } from '@/pages/SettingsPage'
import { SetupPage } from '@/pages/SetupPage'

const rootRoute = createRootRoute({ component: Outlet })

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
  /** `setup: 'complete'` says setup was tried after it was already done. */
  validateSearch: (search: Record<string, unknown>): { setup?: 'complete' } =>
    search.setup === 'complete' ? { setup: 'complete' } : {},
  loader: async ({ abortController }) => {
    const { setupRequired } = await getSetupStatus(abortController.signal)
    if (setupRequired) throw redirect({ to: '/setup', replace: true })
  },
  pendingComponent: PublicPageLoading,
  component: LoginPage,
})

const appRoute = createRoute({
  getParentRoute: () => rootRoute,
  id: 'app',
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
