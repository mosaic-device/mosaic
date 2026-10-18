import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { test } from 'node:test'
import { runInNewContext } from 'node:vm'

const script = readFileSync(
  new URL('../assets/setup.js', import.meta.url),
  'utf8',
)

function harness(states, clock, post = { ok: true, status: 202 }) {
  let elapsed = 0
  clock ??= { now: () => elapsed }
  const timers = new Map()
  let timerId = 0
  const setTimer = (callback, delay) => {
    const id = ++timerId
    const handle = setImmediate(() => {
      timers.delete(id)
      elapsed += delay
      callback()
    })
    timers.set(id, handle)
    return id
  }
  const button = { disabled: false }
  const progress = { textContent: '' }
  let submit
  const requests = []
  const form = {
    action: 'http://192.168.4.1/save',
    querySelector: () => button,
    addEventListener: (_event, callback) => {
      submit = callback
    },
  }
  const document = {
    getElementById: (id) => (id === 'setup-form' ? form : progress),
  }
  class FormData {
    constructor() {
      return [
        ['ssid', 'Open'],
        ['pass', ''],
        ['ws_url', 'wss://example.org/device'],
      ]
    }
  }
  const fetch = async (url, options) => {
    requests.push({ url, options })
    if (url === '/status') {
      const state = states.shift()
      assert.notEqual(state, undefined, 'unexpected extra status request')
      if (typeof state === 'function') return state(options)
      if (state === null) throw new Error('setup AP disconnected')
      return { ok: true, json: async () => ({ state }) }
    }
    assert.equal(url, form.action)
    if (typeof post === 'function') return post(options)
    return post
  }
  runInNewContext(script, {
    document,
    FormData,
    fetch,
    URLSearchParams,
    Date: clock,
    Promise,
    AbortController,
    setTimeout: setTimer,
    clearTimeout: (id) => {
      clearImmediate(timers.get(id))
      timers.delete(id)
    },
  })
  return { button, progress, requests, submit }
}

const settle = () => new Promise((resolve) => setImmediate(resolve))

test('open Wi-Fi submits a blank password and shows saved result', async () => {
  const ui = harness(['ready', 'saving', 'saved'])
  await settle()
  await ui.submit({ preventDefault() {} })
  assert.equal(ui.requests[1].options.method, 'POST')
  assert.equal(
    ui.requests[1].options.headers['Content-Type'],
    'application/x-www-form-urlencoded',
  )
  assert.match(ui.requests[1].options.body, /(?:^|&)pass=(?:&|$)/)
  assert.match(ui.progress.textContent, /Wi-Fi saved/)
  assert.match(
    ui.progress.textContent,
    /reboot in about six seconds, then try connecting/,
  )
  assert.equal(ui.button.disabled, true)
})

test('storage failure keeps the form available without claiming connectivity', async () => {
  const ui = harness(['ready', 'saving', 'storage_error'])
  await settle()
  await ui.submit({ preventDefault() {} })
  assert.match(ui.progress.textContent, /Saving failed/)
  assert.doesNotMatch(ui.progress.textContent, /Connected|Connection failed/)
  assert.equal(ui.button.disabled, false)
})

test('lost setup AP reports an unconfirmed result, not failure', async () => {
  let elapsed = 0
  const clock = { now: () => (elapsed += 1000) }
  const ui = harness(['ready', ...Array(31).fill(null)], clock)
  await settle()
  await ui.submit({ preventDefault() {} })
  assert.match(ui.progress.textContent, /Could not confirm the result here/)
  assert.match(ui.progress.textContent, /device display/)
  assert.doesNotMatch(
    ui.progress.textContent,
    /Connection failed|Saving failed/,
  )
  assert.equal(ui.button.disabled, false)
})

const hangingRequest = () => new Promise(() => {})
const hangingResponse = () => ({ ok: true, json: hangingRequest })

for (const [label, hang] of [
  ['request', hangingRequest],
  ['response JSON', hangingResponse],
]) {
  test(`hanging status ${label} times out and polling recovers`, async () => {
    const ui = harness(['ready', hang, 'saved'])
    await settle()
    await ui.submit({ preventDefault() {} })
    assert.equal(ui.requests[2].options.signal.aborted, true)
    assert.match(ui.progress.textContent, /Wi-Fi saved/)
  })

  test(`hanging initial status ${label} does not block submission`, async () => {
    const ui = harness([hang, 'saved'])
    await settle()
    await ui.submit({ preventDefault() {} })
    assert.match(ui.progress.textContent, /Wi-Fi saved/)
    await settle()
    assert.match(ui.progress.textContent, /Wi-Fi saved/)
    assert.equal(ui.requests[0].options.signal.aborted, true)
  })
}

const interruptedPost = () => {
  throw new Error('setup AP disconnected')
}

for (const [label, post] of [
  ['hanging', hangingRequest],
  ['interrupted', interruptedPost],
]) {
  test(`${label} submission polls and finds saved settings`, async () => {
    const ui = harness(['ready', 'saving', 'saved'], undefined, post)
    await settle()
    await ui.submit({ preventDefault() {} })
    assert.match(ui.progress.textContent, /Wi-Fi saved/)
    assert.equal(ui.button.disabled, true)
    if (label === 'hanging')
      assert.equal(ui.requests[1].options.signal.aborted, true)
  })
}

for (const state of ['storage_error', 'saved']) {
  test(`409 submission polls through saving to ${state}`, async () => {
    const ui = harness(['ready', 'saving', state], undefined, {
      ok: false,
      status: 409,
    })
    await settle()
    await ui.submit({ preventDefault() {} })
    assert.match(
      ui.progress.textContent,
      state === 'saved' ? /Wi-Fi saved/ : /Saving failed/,
    )
    assert.equal(ui.button.disabled, state === 'saved')
  })
}

test('ready after interrupted submission allows retry without claiming failure', async () => {
  const ui = harness(['ready', 'ready'], undefined, interruptedPost)
  await settle()
  await ui.submit({ preventDefault() {} })
  assert.match(ui.progress.textContent, /not confirmed accepted/)
  assert.match(ui.progress.textContent, /device display before retrying/)
  assert.equal(ui.button.disabled, false)
})

test('ambiguous submission with hanging status exhausts polling and allows retry', async () => {
  const ui = harness(
    ['ready', ...Array(6).fill(hangingResponse)],
    undefined,
    interruptedPost,
  )
  await settle()
  await ui.submit({ preventDefault() {} })
  assert.match(ui.progress.textContent, /Could not confirm/)
  assert.match(ui.progress.textContent, /device display before retrying/)
  assert.equal(ui.button.disabled, false)
  for (const request of ui.requests.slice(2))
    assert.equal(request.options.signal.aborted, true)
})

test('late initial poll cannot overwrite a newer submission result', async () => {
  let resolveInitial
  const initial = () =>
    new Promise((resolve) => {
      resolveInitial = resolve
    })
  const ui = harness([initial, 'saved'])
  await ui.submit({ preventDefault() {} })
  resolveInitial({ ok: true, json: async () => ({ state: 'storage_error' }) })
  await settle()
  assert.match(ui.progress.textContent, /Wi-Fi saved/)
  assert.equal(ui.button.disabled, true)
})

test('reopened page resumes saving and reports storage failure honestly', async () => {
  const ui = harness(['saving', 'storage_error'])
  for (let i = 0; i < 4; i++) await settle()
  assert.match(ui.progress.textContent, /Saving failed/)
  assert.doesNotMatch(ui.progress.textContent, /Connected|Connection failed/)
  assert.equal(ui.button.disabled, false)
})
