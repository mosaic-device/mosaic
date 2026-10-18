import { lazy, Suspense } from 'react'
import {
  createRootRoute,
  createRoute,
  createRouter,
  Outlet,
  RouterProvider,
} from '@tanstack/react-router'

const rootRoute = createRootRoute({ component: () => <Outlet /> })
const indexRoute = createRoute({
  getParentRoute: () => rootRoute,
  path: '/',
  component: () => (
    <main className="bg-background text-foreground min-h-screen px-8 py-12">
      <h1 className="text-title leading-8">Admin</h1>
      <p className="text-sm">
        Administration shell. No API requests or authentication yet.
      </p>
    </main>
  ),
})

const previewRoutes = import.meta.env.DEV
  ? [
      createRoute({
        getParentRoute: () => rootRoute,
        path: '/dev/components',
        component: () => (
          <Suspense fallback={<p>Loading preview…</p>}>
            <ComponentPreview />
          </Suspense>
        ),
      }),
    ]
  : []
const ComponentPreview = import.meta.env.DEV
  ? lazy(() => import('./ComponentPreview.js'))
  : () => null
const router = createRouter({
  routeTree: rootRoute.addChildren([indexRoute, ...previewRoutes]),
})

declare module '@tanstack/react-router' {
  interface Register {
    router: typeof router
  }
}

export default function App() {
  return <RouterProvider router={router} />
}
