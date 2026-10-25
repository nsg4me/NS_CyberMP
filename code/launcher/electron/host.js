// Hosting from this PC: run the bundled server, open its port on the router, and start the game connected to it.
// Friends join through Steam (the mod advertises the public address as a Steam "Join Game" connect string).
import { spawn } from 'child_process'
import { promises as fs, existsSync } from 'fs'
import path from 'path'
import electron from 'electron'
import * as upnp from './upnp.js'

const STEAM_APP_ID = 1091500
const DEFAULT_PORT = 11778

let server = null // { process, port }
let mapping = null // { gateway, port }
let status = { state: 'stopped', lines: [] }
let notify = () => {}

function setStatus (patch) {
  status = { ...status, ...patch }
  notify(status)
}

function log (line) {
  const lines = [...status.lines, line].slice(-200)
  setStatus({ lines })
}

export function getStatus () {
  return status
}

export function onStatus (callback) {
  notify = callback
}

// Install root: the folder holding the launcher, "mod" and "server" (same layout the Windows package always had).
export function rootDir () {
  if (process.env.CPMP_ROOT) return process.env.CPMP_ROOT
  return path.dirname(path.dirname(electron.app.getAppPath()))
}

export function modDir () {
  return path.join(rootDir(), 'mod')
}

function serverDir () {
  return path.join(rootDir(), 'server')
}

// No password: the server protocol has no password check, so a password field would only look like protection.
// The server stays private by not being listed; only people you send the address/invite to know it.
async function writeServerConfig ({ name, maxPlayers }) {
  const dir = path.join(serverDir(), 'config')
  const file = path.join(dir, 'server.json')
  let config = {}
  try {
    config = JSON.parse(await fs.readFile(file, 'utf8'))
  } catch {}

  // Every key the server knows, so older server builds (which reject a config with any key missing) accept it too.
  const defaults = {
    Name: 'CyberpunkMP',
    Description: '',
    IconUrl: '',
    MaxPlayer: 4,
    Tags: '',
    TickRate: 60,
    UpdateRate: 10,
    Public: false,
    Port: DEFAULT_PORT,
    Password: '',
    ApiKey: '',
    Flecs: { Enabled: true, Port: 27750, IpAddress: '127.0.0.1' }
  }

  config = {
    ...defaults,
    ...config,
    Name: name || 'CyberpunkMP',
    MaxPlayer: maxPlayers || 4,
    Port: config.Port || DEFAULT_PORT,
    Public: false // reached through invites only, never listed publicly
  }

  await fs.mkdir(dir, { recursive: true })
  await fs.writeFile(file, JSON.stringify(config, null, 4))
  return config
}

function serverCommand () {
  const dir = serverDir()
  if (process.platform === 'win32') return { cmd: path.join(dir, 'Server.Loader.exe'), args: [] }
  const stub = path.join(dir, 'Server.Loader')
  if (existsSync(stub)) return { cmd: stub, args: [] }
  return { cmd: 'dotnet', args: [path.join(dir, 'Server.Loader.dll')] }
}

function startServerProcess () {
  return new Promise((resolve, reject) => {
    const { cmd, args } = serverCommand()
    if (cmd !== 'dotnet' && !existsSync(cmd)) {
      reject(new Error(`Server not found at ${cmd}`))
      return
    }

    const child = spawn(cmd, args, { cwd: serverDir(), windowsHide: true })
    let settled = false
    let produced = false

    const onLine = (data) => {
      for (const line of data.toString().split(/\r?\n/).filter(Boolean)) {
        produced = true
        log(line)
        const started = /Server started on port (\d+)/.exec(line)
        if (started && !settled) {
          settled = true
          resolve({ process: child, port: Number(started[1]) })
        }
      }
    }

    child.stdout.on('data', onLine)
    child.stderr.on('data', onLine)
    child.on('error', (err) => {
      if (!settled) { settled = true; reject(err) }
    })
    child.on('exit', (code) => {
      log(`Server stopped (exit code ${code})`)
      if (!settled) {
        settled = true
        // No output at all usually means the runtime is missing; otherwise the server's own error is in the log above.
        reject(new Error(produced
          ? `The server stopped before starting (exit code ${code}): see its message in the log.`
          : `The server exited without output (exit code ${code}). Is the .NET 8 runtime installed?`))
      }
      if (server && server.process === child) {
        server = null
        setStatus({ state: 'stopped' })
      }
    })

    setTimeout(() => {
      if (!settled) {
        settled = true
        reject(new Error('The server did not report that it started within 60 seconds'))
      }
    }, 60000)
  })
}

async function publicIpFromWeb () {
  const res = await fetch('https://api.ipify.org?format=json')
  return (await res.json()).ip
}

// Open the game port on the router and work out the address friends should use.
async function openToInternet (port) {
  let externalIp = null

  log('Looking for a UPnP router...')
  const gateway = await upnp.findGateway()
  if (gateway) {
    try {
      const local = await upnp.addPortMapping(gateway, port, 'UDP', 'CyberpunkMP')
      mapping = { gateway, port }
      log(`Router opened UDP ${port} to this PC (${local})`)
    } catch (err) {
      log(`Router would not open the port: ${err.message}`)
    }
    try {
      externalIp = await upnp.getExternalIp(gateway)
    } catch {}
  } else {
    log('No UPnP router found.')
  }

  let webIp = null
  try {
    webIp = await publicIpFromWeb()
  } catch {}

  if (externalIp && upnp.isPrivateAddress(externalIp)) {
    log(`Your router's internet address (${externalIp}) is itself private: you are behind a second router or your ISP's CGNAT, so friends probably cannot reach you without port forwarding on the outer router or a VPN.`)
  }

  const publicIp = webIp || (externalIp && !upnp.isPrivateAddress(externalIp) ? externalIp : null)
  if (!mapping) {
    log(`If friends cannot connect, forward UDP ${port} on your router to this PC.`)
  }
  return publicIp
}

async function writeSession (session) {
  await fs.mkdir(modDir(), { recursive: true })
  await fs.writeFile(path.join(modDir(), 'cpmp-session.json'), JSON.stringify(session, null, 2))
}

// Start the game through Steam so the overlay and invites work (and so it runs through Proton on Linux).
// The connect details also go in the session file, which the mod reads if Steam does not pass the arguments on.
export async function launchGame (gamePath, session) {
  await writeSession({ ...session, createdAt: new Date().toISOString() })

  const args = ['--launcher-skip', '--online']
  if (session.ip) args.push(`--ip=${session.ip}`, `--port=${session.port}`)

  const viaSteam = process.platform !== 'win32' || /steamapps/i.test(gamePath || '')
  if (viaSteam) {
    await electron.shell.openExternal(`steam://run/${STEAM_APP_ID}//${encodeURIComponent(args.join(' '))}/`)
    return 'steam'
  }

  // GOG / Epic on Windows: start the executable directly.
  spawn(gamePath, args, { cwd: path.dirname(gamePath), detached: true, stdio: 'ignore' }).unref()
  return 'direct'
}

export async function startHosting ({ name, maxPlayers, gamePath }) {
  if (server) return status

  setStatus({ state: 'starting', lines: [], publicAddress: null, port: null, error: null })
  try {
    await writeServerConfig({ name, maxPlayers })
    log('Starting the server...')
    server = await startServerProcess()
    setStatus({ port: server.port })

    const publicIp = await openToInternet(server.port)
    const publicAddress = publicIp ? `${publicIp}:${server.port}` : null
    setStatus({ state: 'running', publicAddress })
    log(publicAddress
      ? `Friends join at ${publicAddress} (sent automatically with Steam invites)`
      : 'Could not work out your public address: Steam invites will not carry an address.')

    if (gamePath) {
      const how = await launchGame(gamePath, { host: true, ip: '127.0.0.1', port: server.port, publicAddress })
      log(how === 'steam' ? 'Game started through Steam.' : 'Game started.')
    }
  } catch (err) {
    log(`Error: ${err.message}`)
    await stopHosting()
    setStatus({ state: 'stopped', error: err.message })
  }
  return status
}

export async function stopHosting () {
  if (mapping) {
    try {
      await upnp.deletePortMapping(mapping.gateway, mapping.port, 'UDP')
      log(`Router port UDP ${mapping.port} closed`)
    } catch {}
    mapping = null
  }
  if (server) {
    const child = server.process
    server = null
    child.kill()
  }
  setStatus({ state: 'stopped' })
  return status
}
