// Installs the mods CyberpunkMP depends on into the game folder, straight from their GitHub releases.
// Versions are pinned to the set this build was tested with on patch 2.5; bump them here.
import { spawn, execFile } from 'child_process'
import { promises as fs, existsSync } from 'fs'
import os from 'os'
import path from 'path'
import decompress from 'decompress'

export const REQUIREMENTS = [
  { id: 'red4ext', name: 'RED4ext', repo: 'wopss/RED4ext', tag: 'v1.31.0', keyword: 'red4ext', files: ['red4ext/RED4ext.dll', 'bin/x64/winmm.dll'] },
  { id: 'redscript', name: 'redscript', repo: 'jac3km4/redscript', tag: 'v0.5.32', keyword: 'redscript', files: ['engine/tools/scc.exe'] },
  { id: 'codeware', name: 'Codeware', repo: 'psiberx/cp2077-codeware', tag: 'v1.21.0', keyword: 'codeware', files: ['red4ext/plugins/Codeware/Codeware.dll'] },
  { id: 'archivexl', name: 'ArchiveXL', repo: 'psiberx/cp2077-archive-xl', tag: 'v1.28.0', keyword: 'archivexl', files: ['red4ext/plugins/ArchiveXL/ArchiveXL.dll'] },
  { id: 'tweakxl', name: 'TweakXL', repo: 'psiberx/cp2077-tweak-xl', tag: 'v1.12.0', keyword: 'tweakxl', files: ['red4ext/plugins/TweakXL/TweakXL.dll'] },
  { id: 'inputloader', name: 'Input Loader', repo: 'jackhumbert/cyberpunk2077-input-loader', tag: 'v0.2.3', keyword: 'input_loader', files: ['red4ext/plugins/input_loader/input_loader.dll'] }
]

const VC_REDIST_URL = 'https://aka.ms/vc14/vc_redist.x64.exe'
const STEAM_APP_ID = 1091500
const MARKER = path.join('red4ext', 'ns-cybermp-requirements.json')
const GAME_DIRS = new Set(['bin', 'engine', 'r6', 'red4ext', 'archive', 'mods'])

// <game>/bin/x64/Cyberpunk2077.exe -> <game>
export function gameRoot (gamePath) {
  return path.dirname(path.dirname(path.dirname(gamePath)))
}

async function readMarker (root) {
  try {
    return JSON.parse(await fs.readFile(path.join(root, MARKER), 'utf8'))
  } catch {
    return {}
  }
}

async function writeMarker (root, marker) {
  await fs.mkdir(path.join(root, 'red4ext'), { recursive: true })
  await fs.writeFile(path.join(root, MARKER), JSON.stringify(marker, null, 2))
}

export async function status (gamePath) {
  if (!gamePath) return { ready: false, items: [], error: 'Select the game executable first.' }
  const root = gameRoot(gamePath)
  const marker = await readMarker(root)

  const items = REQUIREMENTS.map((req) => {
    const present = req.files.every((file) => existsSync(path.join(root, file)))
    const version = marker[req.id] || null
    // Present without our marker: installed by hand, version unknown. Treated as fine (it may be newer).
    const ok = present && (!version || version === req.tag)
    return { id: req.id, name: req.name, wanted: req.tag, present, version, ok }
  })

  return {
    ready: items.every((item) => item.ok),
    items,
    vcRedist: !!marker.vcredist,
    platform: process.platform
  }
}

async function download (url, file) {
  const res = await fetch(url, { headers: { 'User-Agent': 'NS-CyberMP-launcher' }, redirect: 'follow' })
  if (!res.ok) throw new Error(`download failed (${res.status}) ${url}`)
  await fs.writeFile(file, Buffer.from(await res.arrayBuffer()))
}

async function releaseZip (req) {
  const res = await fetch(`https://api.github.com/repos/${req.repo}/releases/tags/${req.tag}`, {
    headers: { 'User-Agent': 'NS-CyberMP-launcher', Accept: 'application/vnd.github+json' }
  })
  if (!res.ok) throw new Error(`GitHub release ${req.repo} ${req.tag} not found (${res.status})`)
  const release = await res.json()

  const zips = (release.assets || []).filter((a) => /\.zip$/i.test(a.name) && !/pdb|debug|symbol|source|cli/i.test(a.name))
  const asset = zips.find((a) => a.name.toLowerCase().includes(req.keyword)) || zips[0]
  if (!asset) throw new Error(`no zip in ${req.repo} ${req.tag}`)
  return asset
}

// Some zips wrap everything in one folder; the game files start where bin/, r6/, red4ext/... are.
async function contentRoot (dir) {
  const entries = await fs.readdir(dir, { withFileTypes: true })
  if (entries.some((e) => GAME_DIRS.has(e.name.toLowerCase()))) return dir
  const dirs = entries.filter((e) => e.isDirectory())
  if (dirs.length === 1 && entries.length === 1) return contentRoot(path.join(dir, dirs[0].name))
  return dir
}

// Games on NTFS drives under Linux mount files without the exec bit, and Wine refuses to load such DLLs.
async function makeExecutable (dir) {
  if (process.platform !== 'linux') return
  for (const entry of await fs.readdir(dir, { withFileTypes: true, recursive: true })) {
    if (entry.isFile() && /\.(dll|exe|asi)$/i.test(entry.name)) {
      try { await fs.chmod(path.join(entry.parentPath || entry.path, entry.name), 0o755) } catch {}
    }
  }
}

async function installOne (req, root, log) {
  const asset = await releaseZip(req)
  const temp = await fs.mkdtemp(path.join(os.tmpdir(), `cpmp-${req.id}-`))
  try {
    const zip = path.join(temp, asset.name)
    log(`${req.name} ${req.tag}: downloading ${asset.name}...`)
    await download(asset.browser_download_url, zip)

    const extracted = path.join(temp, 'x')
    await decompress(zip, extracted)
    const source = await contentRoot(extracted)
    await fs.cp(source, root, { recursive: true, force: true })
    log(`${req.name} ${req.tag}: installed`)
  } finally {
    await fs.rm(temp, { recursive: true, force: true })
  }
}

function which (cmd) {
  return new Promise((resolve) => {
    execFile(process.platform === 'win32' ? 'where' : 'which', [cmd], (err, out) => resolve(err ? null : out.trim().split('\n')[0]))
  })
}

function run (cmd, args) {
  return new Promise((resolve, reject) => {
    const child = spawn(cmd, args, { stdio: 'ignore' })
    child.on('error', reject)
    child.on('exit', (code) => resolve(code))
  })
}

// The Microsoft C++ runtime RED4ext and the mod are built against. Under Proton it has to go into the game's prefix.
async function installVcRedist (log) {
  const temp = await fs.mkdtemp(path.join(os.tmpdir(), 'cpmp-vc-'))
  try {
    const exe = path.join(temp, 'vc_redist.x64.exe')
    log('Visual C++ runtime: downloading...')
    await download(VC_REDIST_URL, exe)

    if (process.platform === 'win32') {
      const code = await run(exe, ['/install', '/passive', '/norestart'])
      // 0 = installed, 1638 = newer already present, 3010 = needs reboot
      if (![0, 1638, 3010].includes(code)) throw new Error(`installer exit code ${code}`)
      log('Visual C++ runtime: installed')
      return true
    }

    // Native protontricks, or the Flatpak one (common on Fedora-based distros).
    let cmd = await which('protontricks-launch')
    let prefixArgs = []
    if (!cmd && await which('flatpak')) {
      const code = await run('flatpak', ['info', 'com.github.Matoking.protontricks'])
      if (code === 0) {
        cmd = 'flatpak'
        prefixArgs = ['run', `--filesystem=${temp}`, '--command=protontricks-launch', 'com.github.Matoking.protontricks']
      }
    }
    if (!cmd) {
      log('Visual C++ runtime: protontricks is not installed. Install it (Arch/Garuda: sudo pacman -S protontricks; ' +
          'Fedora/Nobara: sudo dnf install protontricks, or flatpak install flathub com.github.Matoking.protontricks), ' +
          'then press Install requirements again.')
      return false
    }
    log('Visual C++ runtime: installing into the game\'s Proton prefix (an installer window may open)...')
    await fs.chmod(exe, 0o755)
    const code = await run(cmd, [...prefixArgs, '--appid', String(STEAM_APP_ID), exe, '/install', '/passive', '/norestart'])
    if (code !== 0) log(`Visual C++ runtime: installer finished with code ${code}; if the game still says RED4ext could not be loaded, run it again`)
    else log('Visual C++ runtime: installed')
    return code === 0
  } finally {
    await fs.rm(temp, { recursive: true, force: true })
  }
}

export async function install (gamePath, log, { force = false } = {}) {
  if (!gamePath) throw new Error('Select the game executable first.')
  const root = gameRoot(gamePath)
  const marker = await readMarker(root)
  const current = await status(gamePath)

  for (const req of REQUIREMENTS) {
    const item = current.items.find((i) => i.id === req.id)
    if (item.ok && !force) {
      log(`${req.name}: already installed${item.version ? ` (${item.version})` : ''}`)
      continue
    }
    await installOne(req, root, log)
    marker[req.id] = req.tag
    await writeMarker(root, marker)
  }

  if (!marker.vcredist || force) {
    try {
      if (await installVcRedist(log)) {
        marker.vcredist = true
        await writeMarker(root, marker)
      }
    } catch (err) {
      log(`Visual C++ runtime: ${err.message}`)
    }
  }

  await makeExecutable(path.join(root, 'bin', 'x64')).catch(() => {})
  await makeExecutable(path.join(root, 'red4ext')).catch(() => {})
  await makeExecutable(path.join(root, 'engine', 'tools')).catch(() => {})

  if (process.platform === 'linux') {
    log('Linux: set the game\'s Steam launch options to   WINEDLLOVERRIDES="winmm=n,b" %command%   (once), or RED4ext will not load.')
  }
  log('Requirements done.')
  return status(gamePath)
}
