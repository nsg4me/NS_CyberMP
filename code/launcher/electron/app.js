import Store from 'electron-store'
import crypto from 'crypto'
import { promises as fs, existsSync } from 'fs'
import decompress from 'decompress'
import path from 'path'
import { fileURLToPath } from 'url'
import electron from 'electron'
import * as host from './host.js'
import * as deps from './deps.js'
import * as updater from './updater.js'
import * as tools from './tools.js'

let win = null

// Put the launcher's mod folder into the game's RED4ext plugins as zzzCyberpunkMP: a junction on Windows, a symlink on
// Linux, or a copy when links are not allowed. Every step is logged in the launcher, since the game silently runs
// without multiplayer when this is missing.
async function installMod (gamePath) {
  const sourceDir = host.modDir()
  const pluginsDir = path.join(path.dirname(path.dirname(path.dirname(gamePath))), 'red4ext', 'plugins')
  const targetDir = path.join(pluginsDir, 'zzzCyberpunkMP')

  if (!existsSync(path.join(sourceDir, 'CyberpunkMP.dll'))) {
    throw new Error(`The mod is missing from ${sourceDir} (CyberpunkMP.dll not found). Extract the whole package again.`)
  }

  // NTFS game drives on Linux: Wine will not load a DLL without the exec bit.
  if (process.platform === 'linux') {
    try { await fs.chmod(path.join(sourceDir, 'CyberpunkMP.dll'), 0o755) } catch {}
  }

  await fs.mkdir(pluginsDir, { recursive: true })

  // Other copies of the mod in plugins/ (renamed manual installs) are loaded too and clash with this one.
  for (const entry of await fs.readdir(pluginsDir, { withFileTypes: true }).catch(() => [])) {
    if (entry.name === 'zzzCyberpunkMP' || !existsSync(path.join(pluginsDir, entry.name, 'CyberpunkMP.dll'))) continue
    const stray = path.join(pluginsDir, entry.name)
    const aside = path.join(path.dirname(pluginsDir), `${entry.name}.disabled-${Date.now()}`)
    try {
      await fs.rename(stray, aside)
      host.note(`Moved a second copy of the mod (${entry.name}) out of the plugins folder, to ${aside}`)
    } catch (err) {
      host.note(`Warning: a second copy of the mod is in ${stray}; remove it, or the mod's scripts will not load (${err.code || err.message})`)
    }
  }

  // Clear what is there: an old link is removed; a real folder (an earlier manual install) is kept aside, not deleted.
  let existing = null
  try { existing = await fs.lstat(targetDir) } catch {}
  if (existing) {
    if (existing.isSymbolicLink()) {
      const current = await fs.readlink(targetDir).catch(() => null)
      if (current && path.resolve(current) === path.resolve(sourceDir)) return 'Mod already linked'
      await fs.unlink(targetDir)
    } else if (existing.isDirectory()) {
      // Out of plugins/: RED4ext loads every folder in there, and a second copy of the mod breaks script compilation.
      const aside = path.join(path.dirname(pluginsDir), `zzzCyberpunkMP.old-${Date.now()}`)
      await fs.rename(targetDir, aside)
      host.note(`Moved an older zzzCyberpunkMP folder out of the plugins folder, to ${aside}`)
    } else {
      await fs.unlink(targetDir)
    }
  }

  try {
    await fs.symlink(sourceDir, targetDir, 'junction')
    host.note(`Mod linked into the game (${targetDir})`)
    return 'linked'
  } catch (error) {
    // Some setups refuse links (permissions, file systems): copy instead. The launcher re-copies on every start.
    host.note(`Could not link the mod (${error.code || error.message}); copying it instead`)
    await fs.cp(sourceDir, targetDir, { recursive: true, force: true })
    host.note(`Mod copied into the game (${targetDir})`)
    return 'copied'
  }
}

const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)
const store = new Store()

const isDev = process.env.ELECTRON_DEV === '1'
const isProd = !isDev

const createWindow = () => {
  win = new electron.BrowserWindow({
    width: 1080,
    height: 850,
    frame: false,
    webPreferences: {
      devTools: isDev,
      webSecurity: isProd,
      nodeIntegration: true,
      preload: isDev ? path.join(__dirname, 'preload.mjs') : path.join(process.resourcesPath, 'preload.mjs')
    }
  })

  if (isDev) {
    win.loadURL('http://localhost:5173')
    win.webContents.openDevTools()
  } else {
    // Packed into app.asar next to this file (a path relative to the working directory breaks when launched elsewhere)
    win.loadFile(path.join(__dirname, 'dist', 'index.html'))
  }

  win.on('ready-to-show', () => {
  })
  win.on('close', function () {
  })
}

electron.app.whenReady().then(async () => {
  electron.ipcMain.handle('get-game-path', () => store.get('game_path'))
  electron.ipcMain.handle('set-game-path', (event, path) => store.set('game_path', path))
  electron.ipcMain.handle('get-user-path', () => electron.app.getPath('userData'))
  electron.ipcMain.handle('minimize-app', () => win.minimize())
  electron.ipcMain.handle('maximize-app', () => win.isMaximized() ? win.unmaximize() : win.maximize())
  electron.ipcMain.handle('close-app', () => electron.app.quit())
  // Hosting stops with the launcher: the server process and the router mapping go away too.
  electron.app.on('before-quit', () => { host.stopHosting() })
  electron.ipcMain.handle('decompress', async (event, src, dest) => {
    return decompress(src, dest)
  })
  electron.ipcMain.handle('select-game-dialog', async () => {
    const result = await electron.dialog.showOpenDialog({
      properties: ['openFile'],
      filters: [{
        name: 'Executable',
        extensions: ['exe']
      }],
      title: 'Select Cyberpunk2077.exe',
      message: 'Please select the Cyberpunk2077.exe file. This file is typically located in bin/x64 of the game\'s installation directory.'
    })
    return result
  })

  electron.ipcMain.handle('create-mod-symlink', async (event, gamePath) => installMod(gamePath))

  // Hosting from this PC and joining through Steam (see host.js)
  host.onStatus((status) => { if (win) win.webContents.send('host-status', status) })
  electron.ipcMain.handle('host-status', () => host.getStatus())
  electron.ipcMain.handle('host-start', async (event, options) => {
    const gamePath = store.get('game_path')
    if (!gamePath) throw new Error('Select the game executable first.')
    await installMod(gamePath)
    let adminPassword = store.get('admin_password')
    if (!adminPassword) {
      adminPassword = crypto.randomBytes(18).toString('base64url')
      store.set('admin_password', adminPassword)
    }
    return host.startHosting({ ...options, gamePath, adminPassword })
  })
  electron.ipcMain.handle('host-stop', () => host.stopHosting())

  // Support tools: log bundle, launch-option check, key bindings
  electron.ipcMain.handle('logs-collect', async () => {
    const gamePath = store.get('game_path')
    return tools.collectLogs({
      gamePath,
      modDir: host.modDir(),
      hostLines: host.getStatus().lines,
      logDir: host.logDir(),
      launcherVersion: updater.currentVersion(),
      requirements: await deps.status(gamePath).catch(() => null)
    })
  })
  electron.ipcMain.handle('launch-option-status', () => tools.launchOptionStatus())
  electron.ipcMain.handle('find-game', () => tools.findGame())
  electron.ipcMain.handle('bindings-get', () => tools.getBindings(host.modDir()))
  electron.ipcMain.handle('bindings-keys', () => tools.KEYS)
  electron.ipcMain.handle('bindings-set', async (event, bindings) => {
    store.set('bindings', bindings)
    return tools.applyBindings(host.modDir(), bindings)
  })
  // Updates replace the mod's input file: put the player's keys back
  const savedBindings = store.get('bindings')
  if (savedBindings) tools.applyBindings(host.modDir(), savedBindings).catch(() => {})

  // Updates from the repo's GitHub Releases
  electron.ipcMain.handle('update-check', () => updater.check())
  electron.ipcMain.handle('app-version', () => updater.currentVersion())
  electron.ipcMain.handle('update-install', async () => {
    await host.stopHosting()
    return updater.install(host.rootDir(),
      (percent) => { if (win) win.webContents.send('update-progress', percent) },
      (line) => { if (win) win.webContents.send('update-log', line) })
  })

  // Required mods (RED4ext, redscript, Codeware, ArchiveXL, TweakXL, Input Loader) and the VC++ runtime
  electron.ipcMain.handle('requirements-status', () => deps.status(store.get('game_path')))
  electron.ipcMain.handle('requirements-install', async (event, options) => {
    const log = (line) => { if (win) win.webContents.send('requirements-log', line) }
    try {
      return await deps.install(store.get('game_path'), log, options || {})
    } catch (err) {
      log(`Error: ${err.message}`)
      throw err
    }
  })
  electron.ipcMain.handle('launch-game', async (event, session) => {
    const gamePath = store.get('game_path')
    if (!gamePath) throw new Error('Select the game executable first.')
    await installMod(gamePath)
    return host.launchGame(gamePath, session)
  })

  if (isDev) {
    try {
      const { default: install, REACT_DEVELOPER_TOOLS } = await import('electron-devtools-installer')

      await install(REACT_DEVELOPER_TOOLS, {
        loadExtensionOptions: {
          allowFileAccess: true
        },
        forceDownload: true
      })
    } catch (error) {
      console.log('Failed to install react-devtools: ', error)
    }
  }

  // createMenu()
  createWindow()
})
