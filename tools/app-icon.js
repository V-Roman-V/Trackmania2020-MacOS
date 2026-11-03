// Copies the largest PNG icon out of a Windows program, for the Trackmania 2020 app.
//
//   osascript -l JavaScript app-icon.js <program.exe> <icon.png>
//
// The icon stays in the installed game, so nothing of Nadeo's is in this repository.
// Trackmania.exe's icon group has a wrong size for its PNG, so this reads the PNG itself.
ObjC.import('Foundation')

const RT_ICON = 3
const PNG = '\x89PNG\r\n\x1a\n'

function run(argv) {
  const data = $.NSData.dataWithContentsOfFile(argv[0])
  if (data.isNil()) throw new Error('Cannot read ' + argv[0])
  // Latin-1 turns every byte into one character with the same code.
  const bytes = (offset, length) => {
    if (offset < 0 || length < 0 || offset + length > data.length) throw new Error('Not a Windows program')
    const range = data.subdataWithRange($.NSMakeRange(offset, length))
    return $.NSString.alloc.initWithDataEncoding(range, $.NSISOLatin1StringEncoding).js
  }
  const u16 = (text, at) => text.charCodeAt(at) | text.charCodeAt(at + 1) << 8
  const u32 = (text, at) => (u16(text, at) | u16(text, at + 2) << 16) >>> 0
  // PNG numbers are big-endian.
  const be32 = (text, at) => (text.charCodeAt(at) << 24 | text.charCodeAt(at + 1) << 16 |
    text.charCodeAt(at + 2) << 8 | text.charCodeAt(at + 3)) >>> 0

  const header = bytes(0, Math.min(4096, data.length))
  const pe = u32(header, 0x3c)
  if (header.slice(0, 2) !== 'MZ' || header.slice(pe, pe + 4) !== 'PE\0\0') throw new Error('Not a Windows program')
  const sections = u16(header, pe + 6)
  const optional = pe + 24
  const directories = optional + (u16(header, optional) === 0x20b ? 112 : 96)
  const rva = u32(header, directories + 16)
  const size = u32(header, directories + 20)
  let start = -1
  for (let i = 0, table = optional + u16(header, pe + 20); i < sections; i++, table += 40) {
    const address = u32(header, table + 12)
    if (address <= rva && rva < address + u32(header, table + 16)) start = u32(header, table + 20) + rva - address
  }
  if (!size || start < 0) throw new Error('No resources')
  const resources = bytes(start, size)
  const entries = at => {
    const found = []
    const count = u16(resources, at + 12) + u16(resources, at + 14)
    for (let i = 0; i < count; i++) found.push([u32(resources, at + 16 + i * 8), u32(resources, at + 20 + i * 8)])
    return found
  }
  const below = offset => offset & 0x7fffffff

  let best = null
  for (const [type, icons] of entries(0)) {
    if (type !== RT_ICON || !(icons & 0x80000000)) continue
    for (const [, languages] of entries(below(icons))) {
      for (const [, leaf] of entries(below(languages))) {
        const at = u32(resources, below(leaf)) - rva
        const length = u32(resources, below(leaf) + 4)
        if (resources.substr(at, 8) !== PNG) continue
        // The width is the first field of the IHDR chunk.
        const width = be32(resources, at + 16)
        if (!best || width > best.width) best = {width, at, length}
      }
    }
  }
  if (!best) throw new Error('No PNG icon')
  if (!data.subdataWithRange($.NSMakeRange(start + best.at, best.length)).writeToFileAtomically(argv[1], true))
    throw new Error('Cannot write ' + argv[1])
}
