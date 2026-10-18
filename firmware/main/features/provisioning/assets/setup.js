;(() => {
  const form = document.getElementById('setup-form')
  const button = form.querySelector('button[type="submit"]')
  const progress = document.getElementById('progress')
  let generation = 0

  function show(message, busy) {
    progress.textContent = message
    button.disabled = busy
  }

  async function request(url, options, json = false, timeout = 5000) {
    const controller = new AbortController()
    let timer
    try {
      return await Promise.race([
        (async () => {
          const response = await fetch(url, {
            ...options,
            signal: controller.signal,
          })
          if (!json) return response
          if (!response.ok) throw new Error('Status unavailable')
          return await response.json()
        })(),
        new Promise((_, reject) => {
          timer = setTimeout(() => {
            controller.abort()
            reject(new Error('Request timed out'))
          }, timeout)
        }),
      ])
    } finally {
      clearTimeout(timer)
    }
  }

  async function poll(version, initial = false, ambiguous = false) {
    const deadline = Date.now() + 30000
    while (version === generation && Date.now() < deadline) {
      try {
        const { state } = await request(
          '/status',
          {
            headers: { Accept: 'application/json' },
            cache: 'no-store',
          },
          true,
          Math.min(5000, deadline - Date.now()),
        )
        if (version !== generation) return
        if (state === 'saved') {
          show(
            'Wi-Fi saved. Mosaic will reboot in about six seconds, then try connecting. Pairing is a separate step.',
            true,
          )
          return
        }
        if (state === 'storage_error') {
          show(
            'Saving failed. Settings were not confirmed saved. Wait a few seconds, then retry.',
            false,
          )
          return
        }
        if (state !== 'saving' && state !== 'ready')
          throw new Error('Unexpected status')
        if (initial && state === 'ready') return
        if (ambiguous && state === 'ready') {
          show(
            'Settings were not confirmed accepted. Check the device display before retrying.',
            false,
          )
          return
        }
        show(
          'Saving settings without testing Wi-Fi… Check the device display if this page disconnects.',
          true,
        )
      } catch {
        if (version !== generation) return
        if (initial) return
        // A lost response does not establish whether settings were saved.
        show(
          'Connection to setup interrupted. Checking again; watch the device display.',
          true,
        )
      }
      initial = false
      await new Promise((resolve) => setTimeout(resolve, 1000))
    }
    if (version === generation && !initial) {
      show(
        'Could not confirm the result here. Check the device display before retrying.',
        false,
      )
    }
  }

  form.addEventListener('submit', async (event) => {
    event.preventDefault()
    const version = ++generation
    show('Sending settings…', true)
    try {
      const response = await request(form.action, {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams(new FormData(form)).toString(),
        cache: 'no-store',
      })
      if (version !== generation) return
      if (!response.ok) {
        if (response.status === 429)
          show('Please wait a few seconds before retrying.', false)
        else if (response.status === 409) {
          show(
            'Settings are already being saved or Mosaic is about to reboot. Checking the result; watch the device display.',
            true,
          )
          await poll(version, false, true)
        } else if (response.status === 400)
          show(
            'Invalid settings. Check the Wi-Fi name, password, and WSS URL.',
            false,
          )
        else
          show(
            'Could not submit settings. Stay on Mosaic Wi-Fi and retry.',
            false,
          )
        return
      }
      show('Saving settings without testing Wi-Fi…', true)
      await poll(version)
    } catch {
      if (version === generation) {
        show(
          'Submission interrupted. Checking whether settings were accepted; watch the device display.',
          true,
        )
        await poll(version, false, true)
      }
    }
  })

  // Recover progress if the page is reopened while settings are being saved.
  void poll(generation, true)
})()
