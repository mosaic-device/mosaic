// Runs the development server and exposes it through Tailscale Funnel.
// Funnel starts only once this server is listening on the address Funnel forwards to,
// so it never publishes another program that holds the port. Stopping either one, or
// pressing Ctrl+C, stops both.
import { spawn, spawnSync } from 'node:child_process'
import { existsSync } from 'node:fs'
import { resolve } from 'node:path'

const root = resolve(import.meta.dirname, '..')
const windows = process.platform === 'win32'

/** The exact address the server binds and Funnel forwards to. */
export const address = 'http://127.0.0.1:5000'

/**
 * Starts `server`, waits until its output says it is listening on `listen`, then starts
 * `funnel`. If the server stops first, for example because another program holds the
 * port, Funnel is never started. Resolves with the exit code once both have stopped.
 */
export function launch({
  server,
  funnel,
  listen,
  cwd = root,
  log = console.error,
}) {
  const children = []
  let stopping = false
  let exitCode = 0
  let resolveDone
  const done = new Promise((resolvePromise) => {
    resolveDone = resolvePromise
  })

  // On macOS and Linux each child gets its own process group, so the whole tree
  // (dotnet run and the server it starts) can be stopped together. On Windows,
  // Ctrl+C already reaches every process in the console, and taskkill /t stops a tree.
  function start({ command, args, env = process.env }, stdout) {
    const child = spawn(command, args, {
      cwd,
      env,
      stdio: ['ignore', stdout, 'inherit'],
      detached: !windows,
    })
    children.push(child)
    child.on('error', (error) => {
      log(`Could not start ${command}: ${error.message}`)
      stopAll(1)
    })
    child.on('exit', (code) => {
      if (child === serverChild && !funnelChild && !stopping) {
        log(
          `The server stopped before it was listening on ${listen}, so Funnel was not started.`,
        )
        stopAll(code || 1)
      } else {
        stopAll(code || 0)
      }
      settle()
    })
    return child
  }

  const running = (child) =>
    child.exitCode === null && child.signalCode === null

  function settle() {
    if (stopping && children.every((child) => !running(child)))
      resolveDone(exitCode)
  }

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

  function stopAll(code, fromConsole = false) {
    if (stopping) return
    stopping = true
    exitCode = code
    // On Windows, Ctrl+C has already reached the children; let them stop cleanly.
    if (!(windows && fromConsole)) {
      for (const child of children.filter(running)) signal(child, 'SIGINT')
      setTimeout(() => {
        for (const child of children.filter(running)) signal(child, 'SIGKILL')
      }, 10_000).unref()
    }
    settle()
  }

  let funnelChild = null
  const serverChild = start(server, 'pipe')

  // Pass the server's output through, watching for Kestrel's line for this exact address.
  // Compare the whole address: `includes` would also accept http://127.0.0.1:50001.
  const listening = (line) =>
    /Now listening on: (\S+)\s*$/.exec(line)?.[1] === listen
  let pending = ''
  serverChild.stdout.on('data', (chunk) => {
    process.stdout.write(chunk)
    if (funnelChild || stopping) return
    pending += chunk.toString()
    const lines = pending.split(/\r?\n/)
    pending = lines.pop()
    if (lines.some(listening)) {
      pending = ''
      funnelChild = start(funnel, 'inherit')
    }
  })

  return { done, stop: (fromConsole = false) => stopAll(0, fromConsole) }
}

function fail(message) {
  console.error(message)
  process.exit(1)
}

function findTailscale() {
  const macAppCli = '/Applications/Tailscale.app/Contents/MacOS/Tailscale'
  if (!spawnSync('tailscale', ['version']).error) return 'tailscale'
  if (process.platform === 'darwin' && existsSync(macAppCli)) return macAppCli
  return null
}

function main() {
  const tailscale = findTailscale()
  if (!tailscale) {
    fail(
      'Tailscale is not installed or its command-line tool is not on PATH. See docs/setup.md#tailscale.',
    )
  }

  const status = spawnSync(tailscale, ['status', '--json'], {
    encoding: 'utf8',
  })
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
  console.log(`Admin app: http://localhost:5000 or pnpm admin:dev\n`)

  const launcher = launch({
    server: {
      command: 'dotnet',
      // Bind exactly the address Funnel forwards to. With `localhost`, Kestrel would carry on
      // with IPv6 alone if another program held 127.0.0.1:5000, and Funnel would publish that
      // program instead; with one exact address, a taken port stops the server.
      args: [
        'run',
        '--project',
        'platform/apps/server/Mosaic.Server.csproj',
        '--',
        '--urls',
        address,
      ],
      // Funnel connects from 127.0.0.1; trust its X-Forwarded-For so login
      // attempts are limited per client instead of shared with local requests.
      env: { ...process.env, ForwardedHeaders__KnownProxies__0: '127.0.0.1' },
    },
    funnel: { command: tailscale, args: ['funnel', address] },
    listen: address,
  })
  for (const name of ['SIGINT', 'SIGTERM', 'SIGHUP']) {
    process.on(name, () => launcher.stop(true))
  }
  launcher.done.then((code) => {
    process.exitCode = code
  })
}

if (import.meta.main) main()
