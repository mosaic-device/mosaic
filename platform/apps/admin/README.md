# @mosaic/admin

Vite/React/Tailwind admin app. It has the signed-in shell (sidebar on wide screens,
top bar and menu sheet on narrow ones) and Home, Devices, Settings and not-found pages
in their empty states. No API integration or authentication is implemented yet.
Routes live in `src/router.ts`, pages in `src/pages/`, the shell in
`src/components/shell/`. `/dev/components` is development-only and excluded from
production. `pnpm build:admin-assets` copies `dist/` into the backend's `wwwroot/`;
local `/api` and `/ws` proxies target `http://localhost:5000`.

Run from the repository root:

```sh
pnpm --filter @mosaic/admin dev
pnpm --filter @mosaic/admin test
pnpm web:check
```

`test` runs the Vitest suite (`src/**/*.test.tsx`: direct loading of every route,
navigation, the phone menu, page states), then builds production and checks preview
exclusion, root-relative asset paths, local fonts, and font notices. After changing primitives, manually check keyboard focus,
dialog trapping/restoration, pending/disabled states, field errors, mobile layout,
forced colors, and reduced motion at `/dev/components`.

## UI constraints

- `src/components/ui/` uses locally adapted shadcn primitives backed by `radix-ui`.
  Keep accessible dialog, checkbox, and switch behavior rather than replacing it
  with handmade interactions.
- `src/index.css` imports assets, tokens, then the opt-in shadcn adapter. Adapter
  values are full CSS colors, not HSL tuples. Keep semantic state overrides local;
  do not replace shared tokens with a CLI preset theme.
- Dark appearance only. Use Manrope 400 for body and 500 for labels, headings, and
  titles; Outfit 500 is reserved for the MOSAIC wordmark.
- Icon-only buttons need `aria-label`. `pending` sets native disabled and
  `aria-busy`; supply meaningful pending text. Pending/disabled actions must remain
  native buttons, not `asChild` links.
- Associate labels with input IDs and help/errors with `aria-describedby`. Use
  `aria-invalid`, native `required`, `name`, and form submission semantics.
- `.list-row` uses `aria-pressed` for independent selections, not listbox or radio
  behavior. Status text must convey meaning without color alone.
- Text utilities: `text-title`, `text-heading`, `text-caption`. For empty, status,
  and loading states use `EmptyState`, `StatusIndicator`, and `PageLoading` from
  `src/components/`.

## Maintaining primitives

`components.json` uses `new-york` Radix style and Tailwind 4's empty config path.
TypeScript and Vite resolve `@/` to `src/`. To regenerate the current primitive set:

```sh
pnpm dlx shadcn@4.21.5 add button label input checkbox switch card separator dialog -c platform/apps/admin -y
```

Review every CLI diff for local adaptations and token overrides. Dependency versions
are recorded in the workspace lockfile.

## Font licenses

`public/licenses/Manrope-OFL.txt` and `public/licenses/Outfit-OFL.txt` are verbatim
copies from `platform/packages/assets/licenses/`, shipped in `dist/licenses/`.
Keep their copyright and license text unchanged when redistributing fonts.
