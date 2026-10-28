import { lazy, Suspense } from 'react'

const ComponentPreview = import.meta.env.DEV
  ? lazy(() => import('./ComponentPreview.js'))
  : () => null

function DevPreview() {
  return (
    <Suspense fallback={<p>Loading preview…</p>}>
      <ComponentPreview />
    </Suspense>
  )
}

export { DevPreview }
