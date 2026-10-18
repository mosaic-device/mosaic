# Mosaic tokens

JSON in `tokens/` is the source of truth. Run `pnpm --filter @mosaic/tokens build`
(or `pnpm generate-tokens` from Mosaic) to regenerate ignored CSS,
ESLint configuration and `firmware/main/ui/generated/design_tokens.h`, then run
the Node built-in tests. `pnpm --filter @mosaic/tokens test` also generates first.
Token JSON, this generator and tests remain committed. Normal web build,
typecheck, lint and dev commands generate automatically; ESP-IDF configure does
the same using Node.js on `PATH`, without pnpm or network access.
`dist/` and the firmware token header are untracked build outputs.
No dependencies, components or font assets are installed by this package.

Dark is the approved appearance. Light retains color-path parity for compatibility
only: it is **unapproved**, not a reviewed theme to expose in product UI.

## Roles

| Semantic token                                      | Dark value / purpose                                                      |
| --------------------------------------------------- | ------------------------------------------------------------------------- |
| `color.background`                                  | `#090A0A` page                                                            |
| `color.surface`, `surfaceMuted`, `muted`            | `#141516` panels / neutral fills                                          |
| `color.surfaceRaised`, `accent`                     | `#222426` raised panels / neutral hover                                   |
| `color.surfaceSunken`                               | background                                                                |
| `color.foreground`, `surfaceForeground`             | `#F2F2EF` essential text                                                  |
| `color.mutedForeground`                             | `#92979B` supporting text                                                 |
| `color.lowEmphasis`                                 | `#666B70` decorative / nonessential only                                  |
| `color.border`, `field`                             | `#303335` decorative separators, not an accessible control boundary alone |
| `color.interactiveBorder`                           | supporting gray; use where a boundary must identify a control             |
| `color.primary` / `primaryForeground`               | `#7540B5` action / white text                                             |
| `color.primaryHover`                                | `#7D4BB9`, rounded RGB mix 6% toward white                                |
| `color.primaryPressed`                              | `#67389F`, rounded RGB mix 12% toward black                               |
| `color.secondary` / `secondaryForeground`           | neutral surface / main text, not purple                                   |
| `color.secondaryHover` / `secondaryPressed`         | raised surface / border                                                   |
| `color.focus`, `selectedForeground`                 | `#B0A3CF` lavender                                                        |
| `color.selected` / `selectedBorder`                 | lavender at 8% / 33% alpha                                                |
| `color.info`, `success`, `warning`, `danger`        | independent blue, green, yellow, red; paired `*Foreground` roles          |
| `focus.width` / `offset`                            | 2px / 4px                                                                 |
| `control.sm` / `md` / `lg` / `touchTarget`          | web 32 / 40 / 48 / 48px                                                   |
| `firmware.control.md`                               | device 80px                                                               |
| `space.*`, `radius.*`, `border.width.*`, `shadow.*` | existing shared scales retained                                           |

Primary normal/hover/pressed with white text pass 4.5:1. Main and supporting text
pass 4.5:1 on background, surface and raised surface. Focus and interactive borders
pass 3:1 on those surfaces. Low-emphasis text and decorative borders intentionally
do not meet these essential-content thresholds. Selected alpha is an additional
cue, not a sufficient accessible control boundary by itself. Status roles are
implementation additions, not alternate brand accents.

## Typography and explicit size mapping

| Web token | Web px | Typography role      | Firmware font px |
| --------- | ------ | -------------------- | ---------------- |
| `text.xs` | 12     | `typography.caption` | 28               |
| `text.sm` | 14     | `typography.label`   | 32               |
| `text.md` | 16     | `typography.body`    | 36               |
| `text.lg` | 20     | `typography.heading` | 48               |
| `text.xl` | 24     | `typography.title`   | 60               |

`font.family.sans` and the preserved `display` name both specify Manrope.
`font.family.brand` specifies Outfit, **weight 500, MOSAIC wordmark only**.
Use normal 400 for body and medium 500 for labels. Approved component headings
and headline/title roles use Manrope 500: `typography.weight.heading` and
`typography.weight.title` both reference `font.weight.medium`. Their generated
CSS variables are `--typography-weight-heading` and `--typography-weight-title`;
firmware receives matching `TOKEN_TYPOGRAPHY_WEIGHT_*` numeric macros only.
The legacy `font.weight.semibold` remains 600; existing weight tokens and
line-height scales remain available.
Finished font/icon files and licenses remain committed in `@mosaic/assets` and
the firmware asset directory. Their sources and pinned generation tooling live
in `platform/packages/assets`; regeneration is an explicit maintenance command,
not part of normal builds. See the [assets README](../assets/README.md) for
provenance and regeneration instructions.

`firmware.text.*` explicitly drives the Manrope 28/32/36/48/60
helper. `TOKEN_TEXT_*` still holds the web size and remains the input to
`GET_FONT(TOKEN_TEXT_*)`; it is not the rendered device pixel size. Do not pass
`TOKEN_FIRMWARE_TEXT_*` to that legacy size-dispatch helper: numeric values can
collide. `GET_FONT` preserves regular 400 behavior; use
`GET_FONT_WEIGHT(TOKEN_TEXT_LG, TOKEN_TYPOGRAPHY_WEIGHT_HEADING)` for 500.
400/500/600 exist at every device size. Unsupported weights resolve to 400;
unknown text roles resolve to body size 36 at the requested supported weight.
`ui_tokens_font_weight(pixel_size, weight)` is the explicit device-pixel API.

## Outputs and consumption

Import `@mosaic/tokens/css/tokens.css` for existing CSS and Tailwind output.
The adapter `@mosaic/tokens/css/shadcn.css` is **opt-in**, imported after tokens.
It maps full CSS colors (`--background`, `--primary`, `--ring`, etc.) to the
semantic variables; consume as `var(--primary)`, not `hsl(var(--primary))`.
Card maps to surface, popover to raised surface, destructive to independent danger,
and shadcn accent stays neutral rather than becoming brand purple. No components
are installed. The adapter does not replace the existing semantic utility API.

Color alpha survives as CSS rgba and separate firmware RGB and 8-bit opacity
(round(alpha × 255)): selected is RGB `0xB0A3CF` / opacity 20, border opacity 84.
Always apply **both** `ui_theme_get_color` and `ui_theme_get_opa` (or matching
`TOKEN_COLOR_*` and `TOKEN_COLOR_*_OPA_*` macros) to the appropriate LVGL style
properties. RGB-only use silently makes translucent selection opaque. Composite
against the actual parent surface; do not assume alpha is precomposited. Existing
RGB-only opaque-token consumers remain compatible. Light legacy surfaces also
contain alpha. Do not use translucent tokens as opaque text or boundary colors.

Tests cover reference failures, theme parity, exact dark palette and mixes,
contrast limitations, CSS/firmware alpha, adapter roles, explicit platform sizes,
and byte-identical repeated generation. Only token-package checks run here;
firmware compilation and hardware validation remain separate baseline work.

Firmware layout roles: `firmware.space.card/control/gap/screen` are 24/20/24/24px;
`firmware.icon.md` is 40px and `firmware.wordmark.md/lg` are 40/60px.
Unknown pixel sizes use body 36/400; unknown legacy roles preserve supported weight.
