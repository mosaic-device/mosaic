import { Outlet } from '@tanstack/react-router'
import { MobileNav } from '@/components/shell/MobileNav'
import { NavLinks } from '@/components/shell/NavLinks'
import { Wordmark } from '@/components/shell/Wordmark'

function AppShell() {
  return (
    <div className="flex min-h-dvh">
      <a
        href="#main-content"
        className="focus:bg-surface-raised sr-only focus:not-sr-only focus:absolute focus:top-4 focus:left-4 focus:z-50 focus:rounded-md focus:px-4 focus:py-2"
      >
        Skip to content
      </a>
      <div className="border-border bg-surface hidden w-60 shrink-0 flex-col gap-8 border-r px-4 py-6 md:sticky md:top-0 md:flex md:h-dvh">
        <Wordmark className="px-3 text-lg" />
        <nav aria-label="Main">
          <NavLinks />
        </nav>
        {/* Signed-in account and logout arrive with session handling (#67). */}
        <div data-slot="account" className="mt-auto" />
      </div>
      <div className="flex min-w-0 flex-1 flex-col">
        <header className="border-border bg-surface flex h-16 items-center justify-between border-b pr-2 pl-5 md:hidden">
          <Wordmark />
          <MobileNav />
        </header>
        <main
          id="main-content"
          tabIndex={-1}
          className="flex-1 px-6 py-10 md:px-10 md:py-12"
        >
          <div className="mx-auto grid max-w-[960px] gap-8">
            <Outlet />
          </div>
        </main>
      </div>
    </div>
  )
}

export { AppShell }
