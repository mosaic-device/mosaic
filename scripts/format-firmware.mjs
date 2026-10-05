import { readdir } from 'node:fs/promises'
import { spawnSync } from 'node:child_process'
import { join } from 'node:path'

const mode = process.argv[2]
if (mode !== '--write' && mode !== '--check') {
  console.error('Usage: node scripts/format-firmware.mjs --write|--check')
  process.exit(2)
}

const extensions = new Set(['.c', '.h', '.cpp', '.hpp'])
const files = []

async function collect(directory) {
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    if (entry.isDirectory() && entry.name !== 'generated') {
      await collect(join(directory, entry.name))
    } else if (
      entry.isFile() &&
      extensions.has(entry.name.slice(entry.name.lastIndexOf('.')))
    ) {
      files.push(join(directory, entry.name))
    }
  }
}

await collect('firmware/main')
files.sort()
if (files.length === 0) {
  console.error('No firmware-owned source files found in firmware/main')
  process.exit(1)
}

const result = spawnSync(
  'clang-format',
  [...(mode === '--write' ? ['-i'] : ['--dry-run', '--Werror']), ...files],
  { stdio: 'inherit' },
)
if (result.error) {
  console.error(result.error.message)
  process.exit(1)
}
process.exit(result.status ?? 1)
