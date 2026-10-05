import { defineConfig } from 'eslint/config'
import js from '@eslint/js'
import reactHooks from 'eslint-plugin-react-hooks'
import reactRefresh from 'eslint-plugin-react-refresh'
import tseslint from 'typescript-eslint'
import { restrictedSyntax } from '@mosaic/tokens/eslint'

export default defineConfig(
  {
    ignores: [
      'dist',
      'node_modules',
      'src-tauri/target/**',
      'src-tauri/gen/**',
    ],
  },
  {
    files: ['**/*.{ts,tsx}'],
    extends: [
      js.configs.recommended,
      ...tseslint.configs.recommended,
      reactHooks.configs['recommended-latest'],
      reactRefresh.configs.vite,
    ],
    rules: { 'no-restricted-syntax': ['error', ...restrictedSyntax] },
  },
)
