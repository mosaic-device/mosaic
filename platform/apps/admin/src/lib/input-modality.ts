/**
 * Marks the page with how the owner last pressed something, as
 * `<html data-input="pointer">` or `"keyboard"`, so `index.css` can hide the
 * keyboard focus ring after a pointer press.
 *
 * Chrome decides `:focus-visible` for focus moved by a script from how focus
 * last moved. Radix menus stop their trigger from taking focus on press and
 * then move focus by script, so a menu opened with a mouse shows the ring on
 * its items, on the trigger after it closes, and on a dialog opened from it.
 */
export function trackInputModality(doc: Document): () => void {
  const root = doc.documentElement
  const onPointer = () => {
    root.dataset.input = 'pointer'
  }
  const onKey = () => {
    root.dataset.input = 'keyboard'
  }
  doc.addEventListener('pointerdown', onPointer, true)
  doc.addEventListener('keydown', onKey, true)
  return () => {
    doc.removeEventListener('pointerdown', onPointer, true)
    doc.removeEventListener('keydown', onKey, true)
  }
}
