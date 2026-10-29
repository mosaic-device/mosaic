import { act, render } from '@testing-library/react'
import { createMemoryHistory, RouterProvider } from '@tanstack/react-router'
import { createAppRouter, type AppRouter } from '@/router'

export async function renderAppAt(path: string): Promise<AppRouter> {
  const router = createAppRouter({
    history: createMemoryHistory({ initialEntries: [path] }),
  })
  await act(() => router.load())
  render(<RouterProvider router={router} />)
  return router
}
