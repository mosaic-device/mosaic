# @mosaic/admin

Vite / React 19 / Tailwind 4 admin foundation. Production retains the neutral
administration scaffold; no API, authentication, or product screens are implemented.
Run workspace commands from `mosaic/`:

```sh
pnpm --filter @mosaic/admin dev
# Open /dev/components (development only)
pnpm --filter @mosaic/admin test
pnpm web:check
```

`test` builds production, then uses Node's test runner to check that preview routes,
copy, and chunks are absent, fonts ship locally, semantic CSS states remain, and
both production OFL notices exactly match the assets source files.
`public/licenses/Manrope-OFL.txt` and `public/licenses/Outfit-OFL.txt` are verbatim
copies from `platform/packages/assets/licenses/`, copied by Vite to `dist/licenses/`.
Keep their copyright and license text unchanged when redistributing the fonts.
No browser test dependency is required. `dist/` is the backend's static copy input.
The local `/api` and `/ws` proxies still target `http://localhost:5000`.

## Foundation

- `src/components/ui/` contains CLI-generated shadcn primitives, adapted locally.
  React primitives consistently use the `radix-ui` backend; do not replace them
  with handmade dialog, checkbox, or switch behavior.
- `src/index.css` imports `@mosaic/assets/fonts.css`, shared token CSS, then the
  explicit shadcn adapter. Adapter colors are complete CSS colors, not HSL tuples.
  Semantic state rules override CLI preset utilities without changing shared tokens.
- Approved dark appearance only; no light-mode control. Manrope 400 body / 500
  labels, headings, and titles. Outfit 500 is reserved for the MOSAIC wordmark.
- `Button` supports default primary, neutral secondary/outline, ghost, destructive,
  and icon sizes. Icon-only actions require `aria-label`. `pending` sets native
  disabled and `aria-busy`; callers supply stable, meaningful pending text.
  Keep pending/disabled actions as native buttons rather than `asChild` links.
- Compose fields with `Label htmlFor`, an input `id`, and `aria-describedby` pointing
  to help/error text. Add `aria-invalid` when invalid. Use native `required`, `name`,
  and form submission semantics. The preview's error is intentionally static.
- `.list-row` uses `aria-pressed` for independent selectable buttons. Selection
  persists independently of focus; it is not a listbox or radio-group substitute.
- Text helpers: `.text-title`, `.text-heading`, `.text-caption`, `.supporting`.
  `.empty-state` is a small layout pattern; status text must communicate meaning
  without color alone. Touch controls, reduced motion, and forced colors are covered
  in the local stylesheet. No decorative animation dependency was added.

## CLI provenance

Stable registry version checked during implementation: **shadcn 4.21.5**.
Components were generated with:

```sh
pnpm dlx shadcn@4.21.5 add button label input checkbox switch card separator dialog -c platform/apps/admin -y
```

`components.json` uses the supported `new-york` Radix style and Tailwind 4's empty
config path. Both TypeScript and Vite resolve `@/` to `src/`. Inspect every future
CLI diff; never replace Mosaic's tokens with a preset theme. The first CLI install
reported pnpm's blocked assets `sharp` build script; a retry successfully generated
all eight components without approving that build script.

Installed versions: `radix-ui` 1.8.1, `lucide-react` 1.56.0,
`class-variance-authority` 0.7.1, `cn` 0.4.0. Exact dependency resolution is in the
workspace lockfile. Fonts use the existing assets workspace package export.

## Browser acceptance

Checked using the available Chrome browser tooling (no extra framework installed):
semantic primary/secondary/destructive colors, Manrope title weight 500, Outfit
wordmark, local-only font requests, pending/disabled attributes, error association,
checkbox click, switch Space-key toggle, Tab focus and lavender outline, dialog
initial focus, reverse-Tab trap, Escape, and trigger focus restoration. Desktop and
390px phone checks found no horizontal overflow; phone controls meet 48px height.
Enlarged body text also remained within the viewport. Repeat these interactions
at `/dev/components` after changing primitive behavior. Forced-colors and
reduced-motion CSS are provided; OS-level visual acceptance remains manual.

Preview code is conditionally imported only under `import.meta.env.DEV`; production
contains no gallery JS chunk or preview route. Shared primitive CSS and fonts remain
part of the foundation intentionally.
