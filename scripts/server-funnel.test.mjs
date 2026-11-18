// Funnel must start only once the launcher's own server listens on the address it forwards to.
import assert from 'node:assert/strict'
import { existsSync, mkdtempSync, rmSync } from 'node:fs'
import { createServer } from 'node:net'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { afterEach, test } from 'node:test'
import { launch } from './server-funnel.mjs'

const directories = []
afterEach(() => {
  for (const directory of directories.splice(0))
    rmSync(directory, { recursive: true, force: true })
})

/** A stand-in for the server: listens on `port`, then prints Kestrel's line for `announce`. */
function fakeServer(port, announce, { delay = 0, exitAfter } = {}) {
  const code = `
    const server = require('node:net').createServer()
    server.on('error', (error) => { console.error(error.code); process.exit(1) })
    server.listen(${port}, '127.0.0.1', () => setTimeout(() => {
      console.log('info: Microsoft.Hosting.Lifetime[14]\\n      Now listening on: ${announce}')
      ${exitAfter === undefined ? '' : `setTimeout(() => process.exit(0), ${exitAfter})`}
    }, ${delay}))`
  return { command: process.execPath, args: ['-e', code] }
}

/** A stand-in for Funnel that records that it started, then runs until stopped. */
function fakeFunnel() {
  const directory = mkdtempSync(join(tmpdir(), 'mosaic-funnel-'))
  directories.push(directory)
  const marker = join(directory, 'started')
  const code = `require('node:fs').writeFileSync(${JSON.stringify(marker)}, ''); setInterval(() => {}, 1000)`
  return { funnel: { command: process.execPath, args: ['-e', code] }, marker }
}

async function freePort() {
  const server = createServer()
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
  const { port } = server.address()
  await new Promise((resolve) => server.close(resolve))
  return port
}

async function waitFor(condition, timeout = 10_000) {
  const deadline = Date.now() + timeout
  while (!condition()) {
    assert.ok(Date.now() < deadline, 'Timed out waiting for the condition.')
    await new Promise((resolve) => setTimeout(resolve, 25))
  }
}

test('a port held by another program stops the launch before Funnel starts', async () => {
  const other = createServer()
  await new Promise((resolve) => other.listen(0, '127.0.0.1', resolve))
  const { port } = other.address()
  const { funnel, marker } = fakeFunnel()
  const messages = []
  try {
    const { done } = launch({
      server: fakeServer(port, `http://127.0.0.1:${port}`),
      funnel,
      listen: `http://127.0.0.1:${port}`,
      log: (message) => messages.push(message),
    })

    assert.equal(await done, 1)
    assert.equal(existsSync(marker), false)
    assert.ok(
      messages.some((message) => message.includes('Funnel was not started')),
    )
  } finally {
    other.close()
  }
})

test('Funnel starts only after the server says it is listening on that address', async () => {
  const port = await freePort()
  const listen = `http://127.0.0.1:${port}`
  const { funnel, marker } = fakeFunnel()
  const launcher = launch({
    server: fakeServer(port, listen, { delay: 500 }),
    funnel,
    listen,
    log: () => {},
  })

  await new Promise((resolve) => setTimeout(resolve, 250))
  assert.equal(existsSync(marker), false)
  await waitFor(() => existsSync(marker))

  launcher.stop()
  assert.equal(await launcher.done, 0)
})

test('listening on another address, such as IPv6 only, does not start Funnel', async () => {
  const port = await freePort()
  const { funnel, marker } = fakeFunnel()
  const { done } = launch({
    server: fakeServer(port, `http://[::1]:${port}`, { exitAfter: 300 }),
    funnel,
    listen: `http://127.0.0.1:${port}`,
    log: () => {},
  })

  assert.equal(await done, 1)
  assert.equal(existsSync(marker), false)
})

test('an address that only starts with the expected one does not start Funnel', async () => {
  const port = await freePort()
  const { funnel, marker } = fakeFunnel()
  const { done } = launch({
    // Kestrel's line for port 50001 begins with the line for port 5000.
    server: fakeServer(port, `http://127.0.0.1:${port}1`, { exitAfter: 300 }),
    funnel,
    listen: `http://127.0.0.1:${port}`,
    log: () => {},
  })

  assert.equal(await done, 1)
  assert.equal(existsSync(marker), false)
})
