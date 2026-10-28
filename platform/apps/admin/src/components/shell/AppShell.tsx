import { Outlet } from '@tanstack/react-router'

function AppShell() {
  return (
    <main id="main-content" tabIndex={-1}>
      <Outlet />
    </main>
  )
}

export { AppShell }
