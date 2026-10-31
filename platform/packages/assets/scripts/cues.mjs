// Renders the device's audio cues from sources/cues.json into 16-bit mono PCM WAV files.
// Offline and dependency-free; run only during asset maintenance.
import fs from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { createHash } from 'node:crypto'
const root = fileURLToPath(new URL('../', import.meta.url))
process.chdir(root)
const out = path.resolve(
  root,
  '../../../firmware/main/features/audio_cues/generated',
)
const sha256 = (bytes) => createHash('sha256').update(bytes).digest('hex')

const spec = JSON.parse(fs.readFileSync('sources/cues.json'))
const rate = spec.sampleRate
const frames = (ms) => Math.round((ms * rate) / 1000)
if (!Number.isInteger(rate) || rate <= 0)
  throw Error('Expected a positive integer sample rate')
if (!(spec.peak > 0 && spec.peak <= 1)) throw Error('Expected a peak in (0, 1]')

// One note: a short attack, a bell-like decay and a release so it never clicks.
function tone(hz, ms) {
  const count = frames(ms)
  const attack = frames(spec.attackMs)
  const release = frames(spec.releaseMs)
  const decay = count / 2.5
  const samples = new Float64Array(count)
  for (let i = 0; i < count; i++) {
    const t = i / rate
    // A little second harmonic keeps the tone audible on a small speaker.
    const wave =
      (Math.sin(2 * Math.PI * hz * t) + 0.2 * Math.sin(4 * Math.PI * hz * t)) /
      1.2
    const envelope =
      Math.min(1, i / attack) *
      Math.exp(-i / decay) *
      Math.min(1, (count - 1 - i) / release)
    samples[i] = wave * envelope
  }
  return samples
}

function render(steps) {
  const parts = [new Float64Array(frames(spec.padMs))]
  for (const step of steps) {
    if (step.rest !== undefined) parts.push(new Float64Array(frames(step.rest)))
    else if (step.hz > 0 && step.ms > 0) parts.push(tone(step.hz, step.ms))
    else throw Error(`Invalid cue step ${JSON.stringify(step)}`)
  }
  parts.push(new Float64Array(frames(spec.padMs)))
  const length = parts.reduce((sum, part) => sum + part.length, 0)
  const pcm = Buffer.alloc(length * 2)
  let offset = 0
  for (const part of parts)
    for (const sample of part) {
      pcm.writeInt16LE(Math.round(sample * spec.peak * 32767), offset)
      offset += 2
    }
  return pcm
}

function wav(pcm) {
  const header = Buffer.alloc(44)
  header.write('RIFF', 0, 'ascii')
  header.writeUInt32LE(36 + pcm.length, 4)
  header.write('WAVE', 8, 'ascii')
  header.write('fmt ', 12, 'ascii')
  header.writeUInt32LE(16, 16) // PCM format chunk size
  header.writeUInt16LE(1, 20) // PCM
  header.writeUInt16LE(1, 22) // mono
  header.writeUInt32LE(rate, 24)
  header.writeUInt32LE(rate * 2, 28) // bytes per second
  header.writeUInt16LE(2, 32) // bytes per frame
  header.writeUInt16LE(16, 34) // bits per sample
  header.write('data', 36, 'ascii')
  header.writeUInt32LE(pcm.length, 40)
  return Buffer.concat([header, pcm])
}

fs.mkdirSync(out, { recursive: true })
const names = Object.keys(spec.cues)
for (const name of fs.readdirSync(out))
  if (name.endsWith('.wav') && !names.includes(name.slice(0, -4)))
    fs.rmSync(path.join(out, name))
const outputs = []
for (const name of names) {
  const file = path.join(out, `${name}.wav`)
  fs.writeFileSync(file, wav(render(spec.cues[name])))
  outputs.push(file)
}
fs.writeFileSync(
  path.join(out, 'README.md'),
  `# Generated Mosaic audio cues

Generated offline with @mosaic/assets from platform/packages/assets/sources/cues.json.
Do not edit generated files; change the cue spec and run the maintenance command in
that package's README.md. Normal builds embed these files without regenerating them.
Checksums are in platform/packages/assets/metadata/cues.json.

Format: ${rate} Hz, mono, 16-bit PCM WAV.

## License

The cues are original to Mosaic, synthesized from the spec above with no
third-party audio, and are covered by the project's license.
`,
)
outputs.push(path.join(out, 'README.md'))
fs.writeFileSync(
  'metadata/cues.json',
  JSON.stringify(
    outputs.map((file) => ({
      file: path.relative(root, file),
      bytes: fs.statSync(file).size,
      sha256: sha256(fs.readFileSync(file)),
    })),
    null,
    2,
  ) + '\n',
)
console.log(`Generated ${names.length} audio cues.`)
