import { cleanup } from '@testing-library/react'
import { afterEach } from 'vitest'

// jsdom has no layout, so the router's scroll restoration has nothing to scroll.
window.scrollTo = () => {}

afterEach(() => {
  cleanup()
})
