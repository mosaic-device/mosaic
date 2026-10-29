import { Outlet } from '@tanstack/react-router'

function PublicLayout() {
  return (
    <main className="grid min-h-dvh place-items-center p-6">
      <Outlet />
    </main>
  )
}

export { PublicLayout }
