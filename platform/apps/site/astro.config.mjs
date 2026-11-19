import { defineConfig } from 'astro/config'
import starlight from '@astrojs/starlight'

export default defineConfig({
  integrations: [
    starlight({
      title: 'Mosaic',
      logo: {
        src: '@mosaic/assets/web/mosaic-wordmark.svg',
        replacesTitle: true,
      },
      social: [
        {
          icon: 'github',
          label: 'GitHub',
          href: 'https://github.com/mosaic-device/mosaic',
        },
      ],
      customCss: [
        '@mosaic/assets/fonts.css',
        '@mosaic/tokens/css/dark.css',
        './src/styles/starlight.css',
      ],
      // The site is dark only.
      components: {
        ThemeProvider: './src/components/DarkTheme.astro',
        ThemeSelect: './src/components/Empty.astro',
      },
      sidebar: [
        { label: 'Docs', items: [{ autogenerate: { directory: 'docs' } }] },
        {
          label: 'Build guide',
          items: [{ autogenerate: { directory: 'build' } }],
        },
      ],
    }),
  ],
})
