# Mosaic tokens

JSON in `tokens/` is the source of truth. Run `pnpm --filter @mosaic/tokens build`
(or `pnpm generate-tokens` from Mosaic) to regenerate ignored CSS,
ESLint configuration and `firmware/main/ui/generated/design_tokens.h`, then run
the Node built-in tests. `pnpm --filter @mosaic/tokens test` also generates first.
Keep JSON, the offline generator, and tests committed; `dist/` and the firmware
header are ignored build outputs. Web build, typecheck, lint, and dev commands
generate automatically. ESP-IDF configure does the same using Node.js on `PATH`,
without pnpm or network access.

Dark is the approved appearance. Light retains color-path parity for compatibility
only: it is **unapproved**, not a reviewed theme to expose in product UI.

## Roles

- Use background/surface roles for layers, foreground for essential text and muted foreground for supporting text.
- Low-emphasis text and decorative borders are nonessential only. Use `color.interactiveBorder` when a boundary identifies a control.
- Primary is the main action; secondary and accent stay neutral. Use paired foreground and hover/pressed roles rather than inventing colors.
- Focus and selection are distinct. Translucent selection alone is not an accessible control boundary.
- Info, success, warning and danger communicate status, not alternate brand accents.

Exact values and scales live in [token JSON](tokens/).

Primary normal/hover/pressed with white text pass 4.5:1. Main and supporting text
pass 4.5:1 on background, surface and raised surface. Focus and interactive borders
pass 3:1 on those surfaces. Low-emphasis text and decorative borders intentionally
do not meet these essential-content thresholds. Selected alpha is an additional
cue, not a sufficient accessible control boundary by itself. Status roles are
not alternate brand accents.

## Typography

Use caption, label, body, heading and title roles rather than hardcoded sizes.
`font.family.sans` and `display` use Manrope; body uses 400 and labels, headings
and titles use 500. Reserve 600 for emphasis. `font.family.brand` is Outfit 500,
for the MOSAIC wordmark only.

Web and firmware have separate size scales. See the
[asset guide](../assets/README.md#firmware-api-and-roles) for firmware size mapping,
font selection APIs and fallbacks, and its [coverage contract](../assets/README.md#coverage-and-fallback-contract)
for supported text. Asset regeneration and licensing are documented there too.

## Outputs and consumption

Import `@mosaic/tokens/css/tokens.css` for existing CSS and Tailwind output.
The adapter `@mosaic/tokens/css/shadcn.css` is **opt-in**, imported after tokens.
It maps full CSS colors (`--background`, `--primary`, `--ring`, etc.) to the
semantic variables; consume as `var(--primary)`, not `hsl(var(--primary))`.
Card maps to surface, popover to raised surface, destructive to independent danger,
and shadcn accent stays neutral rather than becoming brand purple. No components
are installed. The adapter does not replace the existing semantic utility API.

Color alpha survives as CSS rgba and separate firmware RGB and 8-bit opacity
(round(alpha × 255)).
Always apply **both** `ui_theme_get_color` and `ui_theme_get_opa` (or matching
`TOKEN_COLOR_*` and `TOKEN_COLOR_*_OPA_*` macros) to the appropriate LVGL style
properties. RGB-only use silently makes translucent selection opaque. Composite
against the actual parent surface; do not assume alpha is precomposited. Existing
RGB-only opaque-token consumers remain compatible. Light legacy surfaces also
contain alpha. Do not use translucent tokens as opaque text or boundary colors.

Firmware layout uses the `firmware.space.*`, `firmware.icon.*` and
`firmware.wordmark.*` roles. Token tests do not replace firmware compilation or
on-device validation.
