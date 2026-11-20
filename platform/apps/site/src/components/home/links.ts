const repo = 'https://github.com/mosaic-device/mosaic'

export const links = {
  github: repo,
  docs: '/docs/',
  build: '/build/',
  selfHosting: '/docs/',
}

// A folder in the repository, on the main branch.
export const source = (path: string) => `${repo}/tree/main/${path}`
