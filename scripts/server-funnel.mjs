// Runs the development server and exposes it through Tailscale Funnel.
// Stopping either one, or pressing Ctrl+C, stops both.
import { spawn, spawnSync } from 'node:child_process'
import { existsSync } from 'node:fs'
import { resolve } from 'node:path'

const root = resolve(import.meta.dirname, '..')
const port = 5000
const macAppCli = '/Applications/Tailscale.app/Contents/MacOS/Tailscale'
const windows = process.platform === 'win32'

function fail(message) {
  console.error(message)
  process.exit(1)
}

function findTailscale() {
  if (!spawnSync('tailscale', ['version']).error) return 'tailscale'
  if (process.platform === 'darwin' && existsSync(macAppCli)) return macAppCli
  return null
}

const tailscale = findTailscale()
if (!tailscale) {
  fail(
    'Tailscale is not installed or its command-line tool is not on PATH. See docs/setup.md#tailscale.',
  )
}

const status = spawnSync(tailscale, ['status', '--json'], { encoding: 'utf8' })
let state
try {
  state = JSON.parse(status.stdout)
} catch {
  fail(
    `Tailscale is not running. Start it and sign in, then try again.\n${status.stderr ?? ''}`.trim(),
  )
}
if (state.BackendState !== 'Running') {
  fail(
    `Tailscale is not connected (${state.BackendState}). Open Tailscale and sign in, then try again.`,
  )
}
const host = state.Self?.DNSName?.replace(/\.$/, '')
if (!host) {
  fail(
    'This machine has no Tailscale DNS name. Turn on MagicDNS in the Tailscale admin console.',
  )
}

console.log(`\nDevice server URL: wss://${host}/ws/device`)
console.log(`Admin app: http://localhost:${port} or pnpm admin:dev\n`)

// On macOS and Linux each child gets its own process group, so the whole tree
// (dotnet run and the server it starts) can be stopped together. On Windows,
// Ctrl+C already reaches every process in the console, and taskkill /t stops a tree.
function start(command, args, env = process.env) {
  return spawn(command, args, {
    cwd: root,
    env,
    stdio: ['ignore', 'inherit', 'inherit'],
    detached: !windows,
  })
}

const children = [
  start(
    'dotnet',
    ['run', '--project', 'platform/apps/server/Mosaic.Server.csproj'],
    // Funnel connects from 127.0.0.1; trust its X-Forwarded-For so login
    // attempts are limited per client instead of shared with local requests.
    { ...process.env, ForwardedHeaders__KnownProxies__0: '127.0.0.1' },
  ),
  start(tailscale, ['funnel', `http://127.0.0.1:${port}`]),
]

const running = (child) => child.exitCode === null && child.signalCode === null
let stopping = false

function signal(child, name) {
  if (windows) {
    spawnSync('taskkill', ['/pid', String(child.pid), '/t', '/f'], {
      stdio: 'ignore',
    })
  } else {
    try {
      process.kill(-child.pid, name)
    } catch {
      // The process group is already gone.
    }
  }
}

function stopAll(exitCode, fromConsole = false) {
  if (stopping) return
  stopping = true
  process.exitCode = exitCode
  // On Windows, Ctrl+C has already reached the children; let them stop cleanly.
  if (windows && fromConsole) return
  for (const child of children.filter(running)) signal(child, 'SIGINT')
  setTimeout(() => {
    for (const child of children.filter(running)) signal(child, 'SIGKILL')
  }, 10_000).unref()
}

for (const child of children) {
  child.on('error', (error) => {
    console.error(`Could not start ${child.spawnfile}: ${error.message}`)
    stopAll(1)
  })
  child.on('exit', (code) => stopAll(code || 0))
}
for (const name of ['SIGINT', 'SIGTERM', 'SIGHUP']) {
  process.on(name, () => stopAll(0, true))
}
