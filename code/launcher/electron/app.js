import Store from 'electron-store'
import { promises as fs } from 'fs'
import decompress from 'decompress'
import path from 'path'
import { fileURLToPath } from 'url'
import electron from 'electron'
import * as host from './host.js'

let win = null

// Link the launcher's mod folder into the game's RED4ext plugins (a junction on Windows, a symlink on Linux).
async function installMod (gamePath) {
  const sourceDir = host.modDir()
  const targetDir = path.join(path.dirname(path.dirname(path.dirname(gamePath))), 'red4ext', 'plugins', 'zzzCyberpunkMP')

  console.log(`Creating symlink from ${sourceDir} to ${targetDir}`)

  try {
    await fs.unlink(targetDir)
  } catch (error) {
    if (error.code !== 'ENOENT') {
      throw error
    }
  }

  try {
    await fs.mkdir(path.dirname(targetDir), { recursive: true })
    await fs.symlink(sourceDir, targetDir, 'junction')
    return `Symlink created from ${sourceDir} to ${targetDir}`
  } catch (error) {
    if (error.code === 'EEXIST') {
      return 'Symlink already exists'
    } else {
      throw error
    }
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
    if (gamePath) await installMod(gamePath)
    return host.startHosting({ ...options, gamePath })
  })
  electron.ipcMain.handle('host-stop', () => host.stopHosting())
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
