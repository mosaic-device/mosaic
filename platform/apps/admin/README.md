# @mosaic/admin

Vite/React/Tailwind admin app. It has the signed-in shell (sidebar on wide screens,
top bar and menu sheet on narrow ones) and Home, Devices, Settings and not-found pages
in their empty states, the shared API client in `src/api/`, and the first-run setup
and login pages, and session handling. Routes live in `src/router.ts`, pages in
`src/pages/`, the shell in `src/components/shell/`. `/dev/components` is development-only and excluded from
production. `pnpm build:admin-assets` copies `dist/` into the backend's `wwwroot/`;
local `/api` and `/ws` proxies target `http://localhost:5000`.

Run from the repository root after [setup](../../../docs/setup.md). Start the
server first (`pnpm server:dev`), then:

```sh
pnpm admin:dev
pnpm --filter @mosaic/admin test
pnpm web:check
```

`test` runs the Vitest suite (`src/**/*.test.{ts,tsx}`: direct loading of every route,
navigation, the phone menu, page states, the API client and its error states, setup
and login), then builds production and checks preview
exclusion, root-relative asset paths, local fonts, and font notices. After changing primitives, manually check keyboard focus,
dialog trapping/restoration, pending/disabled states, field errors, mobile layout,
forced colors, and reduced motion at `/dev/components`.

## Calling the API

Screens call the functions in `src/api/auth.ts` (`getSetupStatus`, `completeSetup`,
`login`, `logout`, `getSession`), never `fetch` directly. URLs are relative
(`/api/...`): in development the Vite proxy forwards them to the server, and in
production the server serves the app itself, so it is always same-origin. The
contract is the wiki's "Admin API Contract".

Every call returns its data or throws an `ApiError` whose `kind` is:

- `problem`: the server said no. Branch on `code` (never `status` or `title`), show
  `title` as-is, put `fieldErrors` under the matching inputs, and wait
  `retryAfterSeconds` after `too_many_requests`.
- `unavailable`: the server could not be reached, or something other than the API
  answered (the dev proxy with the backend stopped, for example).
- `malformed`: the server's answer did not match the contract.

A cancelled call rejects with the browser's `AbortError` instead. It is not an
error to show; `isAbortError` tells it apart.

- **Reads** belong in a route `loader` or `beforeLoad`. Pass the route's
  `abortController.signal` so leaving the page cancels the call. `PageLoading` shows
  while it loads and `PageError` (with a "Try again" button) if it fails.
- **Writes** go through `useApiAction`. `run(input)` never rejects: it resolves
  `success`, `error` or `cancelled`. Starting another run cancels the one in flight,
  and late results are ignored. Show `error` with `ApiErrorNotice`. Cancelling only
  stops the app waiting: a write the server already received still happens. So keep
  submit disabled while `pending` (the `Button` `pending` prop) instead of letting a
  second click replace the first. On-mount calls belong in a `loader`, not here.

The dev proxy needs the server in the Development environment, as `pnpm server:dev`
runs it (non-Secure cookie, `http://localhost:5173` allowed by the Origin check).
Vite is pinned to port 5173 (`strictPort`) for the same reason.

## Setup and login

`/setup` and `/login` sit outside the signed-in shell. Both ask the server whether
an admin exists (`GET /api/setup`) and redirect to the other page when needed, so
either address works. Setup needs the one-time code the server prints when it
starts with no admin. The forms check fields with the server's rules first
(`src/api/credential-rules.ts`; login only checks that fields are filled in, so
it never reveals the password rules), then show server field messages under
their fields and other errors above the form. After `too_many_requests` the
submit button waits out `Retry-After`. Success goes to the page the owner was
trying to open, or Home.

## Sessions and logout

The session cookie is `HttpOnly`, so the app asks the server instead of reading
it: the `app` route's `beforeLoad` calls `GET /api/auth/session` on every
navigation in the signed-in shell. Until it answers, only a centered spinner
shows, and a server that can't be reached shows the error page without the
shell. A `401` sends the owner to `/login?redirect=<page>`; `redirect` is kept
only when it is a path inside the app (`src/lib/return-path.ts`). When this tab
was signed in and lost its session, login also says the session ended. Login
skips the form for someone already signed in. The shell checks again when the
tab comes back into view, acting only on a `401`. Logout sits at the bottom of
the sidebar and the phone menu; if the server never hears it, the owner stays
signed in and sees the error.

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
  loading and error states use `EmptyState`, `StatusIndicator`, `PageLoading`,
  `PageError` and `ApiErrorNotice` from `src/components/`. Form inputs use
  `TextField`, which links the label, hint and error, and gives
  `type="password"` a "Show password" toggle.

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
