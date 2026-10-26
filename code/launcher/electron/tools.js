// Support tools: a log bundle for troubleshooting, the Linux launch-option check, and key rebinding.
import { promises as fs, existsSync } from 'fs'
import { execFile } from 'child_process'
import os from 'os'
import path from 'path'
import AdmZip from 'adm-zip'
import electron from 'electron'

const STEAM_APP_ID = '1091500'

// --- Log bundle ---------------------------------------------------------------------------------------------------

async function newestFiles (dir, pattern, count) {
  try {
    const entries = await fs.readdir(dir)
    const files = await Promise.all(entries.filter((n) => pattern.test(n)).map(async (n) => {
      const full = path.join(dir, n)
      return { full, mtime: (await fs.stat(full)).mtimeMs }
    }))
    return files.sort((a, b) => b.mtime - a.mtime).slice(0, count).map((f) => f.full)
  } catch {
    return []
  }
}

// Everything needed to diagnose a problem, in one zip in Downloads.
export async function collectLogs ({ gamePath, modDir, hostLines, launcherVersion, requirements }) {
  const zip = new AdmZip()
  const add = async (file, name) => {
    try {
      zip.addFile(name, await fs.readFile(file))
      return true
    } catch {
      return false
    }
  }

  const included = []
  if (await add(path.join(modDir, 'CyberpunkMP.log'), 'CyberpunkMP.log')) included.push('CyberpunkMP.log')

  if (gamePath) {
    const root = path.dirname(path.dirname(path.dirname(gamePath)))
    // Newest log of each kind: RED4ext itself, each plugin, and redscript's compiler output.
    const sources = [
      [path.join(root, 'red4ext', 'logs'), /^red4ext-.*\.log$/i, 'red4ext'],
      [path.join(root, 'red4ext', 'logs'), /^(?!red4ext-).*\.log$/i, 'red4ext'],
      [path.join(root, 'r6', 'logs'), /\.log$/i, 'r6']
    ]
    for (const [dir, pattern, prefix] of sources) {
      const seen = new Set()
      for (const file of await newestFiles(dir, pattern, 20)) {
        const kind = path.basename(file).replace(/[-_]?\d{4}-\d{2}-\d{2}.*$/, '').replace(/\.log$/i, '').toLowerCase()
        if (seen.has(kind)) continue
        seen.add(kind)
        if (await add(file, `${prefix}/${path.basename(file)}`)) included.push(`${prefix}/${path.basename(file)}`)
      }
    }

    // Copies of the mod RED4ext would load (a second one breaks script compilation)
    const plugins = path.join(root, 'red4ext', 'plugins')
    const copies = []
    for (const entry of await fs.readdir(plugins, { withFileTypes: true }).catch(() => [])) {
      if (existsSync(path.join(plugins, entry.name, 'CyberpunkMP.dll'))) copies.push(entry.name)
    }
    zip.addFile('mod-copies.txt', Buffer.from(copies.join('\n') || '(none)'))
  }

  // Proton's log (only written with PROTON_LOG=1); skip stale ones
  const protonLog = path.join(os.homedir(), `steam-${STEAM_APP_ID}.log`)
  try {
    const stat = await fs.stat(protonLog)
    if (Date.now() - stat.mtimeMs < 24 * 3600 * 1000 && await add(protonLog, 'proton.log')) included.push('proton.log')
  } catch {}

  // Whether (and how) the mod is installed in the game: the first thing to check when multiplayer does not load.
  let modInGame = 'unknown'
  if (gamePath) {
    const target = path.join(path.dirname(path.dirname(path.dirname(gamePath))), 'red4ext', 'plugins', 'zzzCyberpunkMP')
    try {
      const st = await fs.lstat(target)
      modInGame = st.isSymbolicLink() ? `link -> ${await fs.readlink(target).catch(() => '?')}` : (st.isDirectory() ? 'folder (copy)' : 'file?')
      modInGame += existsSync(path.join(target, 'CyberpunkMP.dll')) ? ', CyberpunkMP.dll present' : ', CyberpunkMP.dll MISSING'
    } catch {
      modInGame = 'missing (zzzCyberpunkMP not in red4ext/plugins)'
    }
  }

  const info = {
    launcherVersion,
    modInGame,
    modDir,
    modVersion: (await fs.readFile(path.join(modDir, 'version.txt'), 'utf8').catch(() => 'unknown')).trim(),
    platform: `${process.platform} ${os.release()}`,
    gamePath,
    requirements,
    launchOption: process.platform === 'linux' ? await launchOptionStatus() : undefined,
    created: new Date().toISOString()
  }
  zip.addFile('info.json', Buffer.from(JSON.stringify(info, null, 2)))
  zip.addFile('launcher-host.log', Buffer.from((hostLines || []).join('\n')))

  const stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)
  const out = path.join(electron.app.getPath('downloads'), `NS-CyberMP-logs-${stamp}.zip`)
  zip.writeZip(out)
  electron.shell.showItemInFolder(out)
  return { file: out, included }
}

// --- Linux: Steam launch option --------------------------------------------------------------------------------------

function steamRoots () {
  const home = os.homedir()
  return [
    path.join(home, '.local', 'share', 'Steam'),
    path.join(home, '.steam', 'steam'),
    path.join(home, '.var', 'app', 'com.valvesoftware.Steam', '.local', 'share', 'Steam')
  ]
}

// Reads Cyberpunk's launch options from Steam's localconfig.vdf (per Steam account) without changing anything.
export async function launchOptionStatus () {
  if (process.platform !== 'linux') return { checked: false, ok: true }

  const values = []
  for (const root of steamRoots()) {
    const userdata = path.join(root, 'userdata')
    if (!existsSync(userdata)) continue
    for (const account of await fs.readdir(userdata).catch(() => [])) {
      const file = path.join(userdata, account, 'config', 'localconfig.vdf')
      let text
      try { text = await fs.readFile(file, 'utf8') } catch { continue }

      const start = text.search(new RegExp(`"${STEAM_APP_ID}"\\s*\\{`))
      if (start < 0) continue
      // The app's block ends at its matching brace
      let depth = 0
      let end = start
      for (let i = text.indexOf('{', start); i < text.length; i++) {
        if (text[i] === '{') depth++
        else if (text[i] === '}' && --depth === 0) { end = i; break }
      }
      const block = text.slice(start, end)
      const match = /"LaunchOptions"\s*"((?:[^"\\]|\\.)*)"/.exec(block)
      values.push({ account, value: match ? match[1].replace(/\\"/g, '"').replace(/\\\\/g, '\\') : '' })
    }
  }

  if (values.length === 0) return { checked: false, ok: true }
  const ok = values.some((v) => /WINEDLLOVERRIDES\s*=\s*["']?[^"']*winmm\s*=\s*n/i.test(v.value))
  return { checked: true, ok, values }
}

// --- Finding the game ---------------------------------------------------------------------------------------------

function windowsSteamPath () {
  return new Promise((resolve) => {
    execFile('reg', ['query', 'HKCU\\Software\\Valve\\Steam', '/v', 'SteamPath'], { windowsHide: true }, (err, out) => {
      if (err) return resolve(null)
      const match = /SteamPath\s+REG_SZ\s+(.+)/.exec(out)
      resolve(match ? match[1].trim().replace(/\//g, '\\') : null)
    })
  })
}

// Every Steam library folder (Steam's libraryfolders.vdf), then the one holding Cyberpunk 2077.
export async function findGame () {
  const roots = process.platform === 'win32'
    ? [await windowsSteamPath(), 'C:\\Program Files (x86)\\Steam'].filter(Boolean)
    : steamRoots()

  const libraries = new Set()
  for (const root of roots) {
    libraries.add(root)
    for (const vdf of [path.join(root, 'steamapps', 'libraryfolders.vdf'), path.join(root, 'config', 'libraryfolders.vdf')]) {
      let text
      try { text = await fs.readFile(vdf, 'utf8') } catch { continue }
      for (const match of text.matchAll(/"path"\s*"((?:[^"\\]|\\.)*)"/g)) libraries.add(match[1].replace(/\\\\/g, '\\'))
    }
  }

  for (const library of libraries) {
    const exe = path.join(library, 'steamapps', 'common', 'Cyberpunk 2077', 'bin', 'x64', 'Cyberpunk2077.exe')
    if (existsSync(exe)) return exe
  }

  // GOG / Epic default folders
  const others = process.platform === 'win32'
    ? ['C:\\Program Files (x86)\\GOG Galaxy\\Games\\Cyberpunk 2077', 'C:\\Program Files\\Epic Games\\Cyberpunk 2077']
    : []
  for (const dir of others) {
    const exe = path.join(dir, 'bin', 'x64', 'Cyberpunk2077.exe')
    if (existsSync(exe)) return exe
  }
  return null
}

// --- Key bindings ----------------------------------------------------------------------------------------------------

// Friendly actions -> mappings in the mod's Input Loader file (some actions share one key).
export const ACTIONS = [
  { id: 'connect', label: 'Connect / disconnect (hold)', mappings: ['UIConnectToServer_Button', 'UIDisconnectFromServer_Button'] },
  { id: 'chat', label: 'Open chat', mappings: ['UIEnterChatMessage_Button'] },
  { id: 'emote', label: 'Emote wheel', mappings: ['UIEmote_Button'] },
  { id: 'jobs', label: 'Jobs / shop (hold)', mappings: ['UIShop_Button', 'UIJob_Button'] },
  { id: 'chatUp', label: 'Scroll chat up', mappings: ['ChatTop_Button'] },
  { id: 'chatDown', label: 'Scroll chat down', mappings: ['ChatBottom_Button'] }
]

export const KEYS = [
  ...'ABCDEFGHIJKLMNOPQRSTUVWXYZ'.split('').map((c) => `IK_${c}`),
  ...'0123456789'.split('').map((c) => `IK_${c}`),
  ...Array.from({ length: 12 }, (_, i) => `IK_F${i + 1}`),
  'IK_Slash', 'IK_Period', 'IK_Comma', 'IK_Semicolon', 'IK_SingleQuote', 'IK_LeftBracket', 'IK_RightBracket',
  'IK_Backslash', 'IK_Minus', 'IK_Equals', 'IK_Tilde', 'IK_PageUp', 'IK_PageDown', 'IK_Home', 'IK_End', 'IK_Insert',
  'IK_Delete', 'IK_NumPad0', 'IK_NumPad1', 'IK_NumPad2', 'IK_NumPad3', 'IK_NumPad4', 'IK_NumPad5', 'IK_NumPad6',
  'IK_NumPad7', 'IK_NumPad8', 'IK_NumPad9', 'IK_Mouse4', 'IK_Mouse5'
]

function inputsFile (modDir) {
  return path.join(modDir, 'assets', 'Inputs', 'CyberpunkMP.xml')
}

function mappingRegex (mapping) {
  return new RegExp(`(<mapping\\s+name="${mapping}"[^>]*>\\s*<button\\s+id=")([^"]+)(")`)
}

export async function getBindings (modDir) {
  const xml = await fs.readFile(inputsFile(modDir), 'utf8')
  return ACTIONS.map((action) => {
    const match = mappingRegex(action.mappings[0]).exec(xml)
    return { id: action.id, label: action.label, key: match ? match[2] : null }
  })
}

// Writes the keys into the mod's input file; the game picks them up on its next start.
export async function applyBindings (modDir, bindings) {
  const file = inputsFile(modDir)
  let xml = await fs.readFile(file, 'utf8')
  for (const action of ACTIONS) {
    const key = bindings[action.id]
    if (!key || !KEYS.includes(key)) continue
    for (const mapping of action.mappings) xml = xml.replace(mappingRegex(mapping), `$1${key}$3`)
  }
  await fs.writeFile(file, xml)
  return getBindings(modDir)
}
