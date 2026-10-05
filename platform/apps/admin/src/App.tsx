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
      <h1 className="text-2xl font-semibold">Admin</h1>
      <p className="mt-3 text-sm">
        Administration shell. No API requests or authentication yet.
      </p>
    </main>
  ),
})

const router = createRouter({ routeTree: rootRoute.addChildren([indexRoute]) })

declare module '@tanstack/react-router' {
  interface Register {
    router: typeof router
  }
}

export default function App() {
  return <RouterProvider router={router} />
}
