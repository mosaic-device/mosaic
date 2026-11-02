import { Outlet } from '@tanstack/react-router'
import type { ReactNode } from 'react'

/** The centered frame for pages outside the signed-in shell. */
function PublicFrame({ children }: { children: ReactNode }) {
  return (
    <main className="grid min-h-dvh place-items-center p-6">{children}</main>
  )
}

function PublicLayout() {
  return (
    <PublicFrame>
      <Outlet />
    </PublicFrame>
  )
}

export { PublicFrame, PublicLayout }
