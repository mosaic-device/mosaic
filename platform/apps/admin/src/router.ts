import {
  createRootRoute,
  createRoute,
  createRouter,
  Outlet,
  type RouterHistory,
} from '@tanstack/react-router'
import { PageError } from '@/components/PageError'
import { PageLoading } from '@/components/PageLoading'
import { AppShell } from '@/components/shell/AppShell'
import { PublicLayout } from '@/components/shell/PublicLayout'
import { DevPreview } from '@/DevPreview'
import { DevicesPage } from '@/pages/DevicesPage'
import { HomePage } from '@/pages/HomePage'
import { NotFoundPage } from '@/pages/NotFoundPage'
import { SettingsPage } from '@/pages/SettingsPage'

const rootRoute = createRootRoute({ component: Outlet })

// Setup and login (#66) render here, outside the signed-in shell.
const publicRoute = createRoute({
  getParentRoute: () => rootRoute,
  id: 'public',
  component: PublicLayout,
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
  publicRoute,
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
