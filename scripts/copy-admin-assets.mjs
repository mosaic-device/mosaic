import { cp, mkdir, readdir, rm } from 'node:fs/promises'
import { resolve } from 'node:path'

const root = resolve(import.meta.dirname, '..')
const source = resolve(root, 'platform/apps/admin/dist')
const destination = resolve(root, 'platform/apps/backend/wwwroot')

if (!(await readdir(source)).includes('index.html')) {
  throw new Error('Build admin before copying: dist/index.html is missing')
}
await rm(destination, { recursive: true, force: true })
await mkdir(destination, { recursive: true })
await cp(source, destination, { recursive: true })
console.log(`Copied admin assets to ${destination}`)
