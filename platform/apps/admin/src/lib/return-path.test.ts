import { describe, expect, it } from 'vitest'
import { returnPathFor, safeReturnPath } from '@/lib/return-path'

describe('safeReturnPath', () => {
  it('keeps paths inside the app', () => {
    expect(safeReturnPath('/devices')).toBe('/devices')
    expect(safeReturnPath('/devices?tab=a#x')).toBe('/devices?tab=a#x')
  })

  it('keeps encoded slashes as a path inside the app', () => {
    expect(safeReturnPath('/%2F%2Fevil.example')).toBe('/%2F%2Fevil.example')
  })

  it('drops Home, since login goes there anyway', () => {
    expect(safeReturnPath('/')).toBeUndefined()
  })

  it('drops login and setup', () => {
    expect(safeReturnPath('/login')).toBeUndefined()
    expect(safeReturnPath('/login?x=1')).toBeUndefined()
    expect(safeReturnPath('/setup')).toBeUndefined()
  })

  it.each([
    '//evil.example',
    '/\\evil.example',
    '/\t/evil.example',
    '/.//evil.example',
    '/..//evil.example',
    '/%2e%2e//evil.example',
    'https://evil.example/x',
    'javascript:alert(1)',
    'devices',
    '',
    42,
    undefined,
  ])('drops %j, which could leave the app', (value) => {
    expect(safeReturnPath(value)).toBeUndefined()
  })
})

describe('returnPathFor', () => {
  it('turns a router location into a return path', () => {
    expect(returnPathFor('/settings?x=1')).toBe('/settings?x=1')
    expect(returnPathFor('/')).toBeUndefined()
  })
})
