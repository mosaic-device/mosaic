# @mosaic/admin

Vite/React/Tailwind admin app. It has the signed-in shell (sidebar on wide screens,
top bar and menu sheet on narrow ones), Home with the paired devices, Settings and
not-found pages in their empty states, the shared API client in `src/api/`, the
first-run setup and login pages, session handling, and pairing, listing, removing
and sending a test notification to devices. Routes live in `src/router.ts`, pages in
`src/pages/`, the shell in `src/components/shell/`. `/dev/components` is development-only and excluded from
production. `pnpm build:admin-assets` copies `dist/` into the server's `wwwroot/`;
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
and login, pairing, the device list and test notifications, Home's device card), then builds production and checks preview
exclusion, root-relative asset paths, local fonts, and font notices. After changing primitives, manually check keyboard focus,
dialog trapping/restoration, pending/disabled states, field errors, mobile layout,
forced colors, and reduced motion at `/dev/components`.

## Calling the API

Screens call the functions in `src/api/auth.ts` (`getSetupStatus`, `completeSetup`,
`login`, `logout`, `getSession`) and `src/api/devices.ts` (`claimDevice`,
`listDevices`, `removeDevice`, `sendTestNotification`), never
`fetch` directly. URLs are relative
(`/api/...`): in development the Vite proxy forwards them to the server, and in
production the server serves the app itself, so it is always same-origin. The
contract is the wiki's "Admin API Contract".

Every call returns its data or throws an `ApiError` whose `kind` is:

- `problem`: the server said no. Branch on `code` (never `status` or `title`), show
  `title` as-is, put `fieldErrors` under the matching inputs, and wait
  `retryAfterSeconds` after `too_many_requests`.
- `unavailable`: the server could not be reached, or something other than the API
  answered (the dev proxy with the server stopped, for example).
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

## Pairing a device

`/devices/pair` (the "Pair a device" button on Devices) claims the device showing
a code with `POST /api/devices/claim`. The code can be typed in any case, with or
without the dash; the form checks its shape with the server's rule first
(`validateClaim`). An unknown, expired or already used code shows the server's
`title` under the field, which says what to enter instead. A failure where the
claim may have gone through anyway (server out of reach, an unexpected answer,
`internal_error`) never looks like success: it asks the owner to try the same code
again, which the server answers with the same device. A `401` re-runs the route
guard, so login opens with a way back. Success shows the device as paired, and
as online or offline. A "Devices" link with a back arrow above the title leads
back (`PageHeader`'s `back` option).

## Paired devices

`/devices` lists the paired devices from `GET /api/devices` (loaded by the route),
oldest first. Devices have no names yet, so each is "Mosaic" plus its ID. Every row
says when it was paired and whether it's online right now. The server never pushes
changes, so `useDeviceList` asks again every 10 seconds while the tab is visible and
as soon as the owner comes back to it; a failed check keeps the last list and says
when the server last answered. Each row's actions menu (⋯) holds Send test and
Remove. Remove opens a dialog that stays open until the
server answers `DELETE /api/devices/{id}`. A `404 device_not_found` counts as
removed (the contract's "already removed"). After a removal the row goes and
"Mosaic 3 removed." takes focus. A failure where the removal may have gone through
anyway asks the owner to try again, which is safe.

Send test calls `POST /api/devices/{id}/test-notification`, which waits up to 10
seconds for the device to confirm. A banner under that row says "Waiting for Mosaic
3 to answer…", then what the server reported: delivered, offline (nothing sent), no
answer, or rejected (a server bug the server logs). Each of these is a `200`, and the
list checks again right away so Online and Offline agree. The banner stays until the
next send. A request failure shows the error in the row, plus "The app didn't hear
whether it arrived." when it may have been sent. `429 too_many_requests` (another
test to that device is still waiting) says so in the banner and turns Send test into
"Try again in N seconds" for `Retry-After` (kept to 1 to 10), then the banner clears.
`404 device_not_found` drops the row with "Mosaic 3 isn't paired anymore, so nothing
was sent." above the list.

Home's Device card loads the same list (both routes share the loader) and stays
current the same way: "Not paired" with a link to pair, one device's Online or
Offline and paired date, or "1 of 2 online" for several, with a link to Devices.
When a check fails, the card keeps the last report, drops the green dot and says
when the server last answered. If the first load fails, Home shows the error page
with "Try again", as Devices does.

## UI constraints

- `src/components/ui/` uses locally adapted shadcn primitives backed by `radix-ui`.
  Keep accessible dialog, menu, checkbox, and switch behavior rather than replacing it
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
pnpm dlx shadcn@4.21.5 add button label input checkbox switch card separator dialog dropdown-menu -c platform/apps/admin -y
```

Review every CLI diff for local adaptations and token overrides. Dependency versions
are recorded in the workspace lockfile.

## Font licenses

`public/licenses/Manrope-OFL.txt` and `public/licenses/Outfit-OFL.txt` are verbatim
copies from `platform/packages/assets/licenses/`, shipped in `dist/licenses/`.
Keep their copyright and license text unchanged when redistributing fonts.
