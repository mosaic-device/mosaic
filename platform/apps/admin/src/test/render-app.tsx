import { act, render } from '@testing-library/react'
import { createMemoryHistory, RouterProvider } from '@tanstack/react-router'
import { createAppRouter, type AppRouter } from '@/router'
import { stubSignedIn } from '@/test/fetch-stub'

export async function renderAppAt(path: string): Promise<AppRouter> {
  const router = createAppRouter({
    history: createMemoryHistory({ initialEntries: [path] }),
  })
  await act(() => router.load())
  render(<RouterProvider router={router} />)
  return router
}

/** Opens a signed-in page, with every session check answered as signed in. */
export function renderSignedInAt(path: string): Promise<AppRouter> {
  stubSignedIn()
  return renderAppAt(path)
}
