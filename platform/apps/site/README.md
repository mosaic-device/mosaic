# Site

The Mosaic marketing site, docs and build guide, built with [Astro](https://astro.build) and [Starlight](https://starlight.astro.build). The plan for its content is in [`docs/marketing-site.md`](../../../docs/marketing-site.md).

Run it from the repository root:

```sh
pnpm site:dev
```

- `src/pages/index.astro` is the homepage, laid out by hand.
- `src/content/docs/docs/` holds the docs, served under `/docs/`.
- `src/content/docs/build/` holds the build guide, served under `/build/`.

The device images are renders from `@mosaic/assets/renders/`, made with `pnpm render-site`. The S5-to-S6 section plays them as frames, pinned while you scroll (`src/scripts/journey.ts`).

Starlight's colours and fonts come from `@mosaic/tokens` and `@mosaic/assets` (`src/styles/starlight.css`). The site is dark only, so Starlight's theme switcher is removed.

`pnpm web:check` typechecks and builds it with the other web apps.
