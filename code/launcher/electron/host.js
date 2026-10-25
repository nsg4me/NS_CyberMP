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

function startServerProcess (adminPassword) {
  return new Promise((resolve, reject) => {
    const { cmd, args } = serverCommand()
    if (cmd !== 'dotnet' && !existsSync(cmd)) {
      reject(new Error(`Server not found at ${cmd}`))
      return
    }

    // The server's admin web API (port 11779) refuses to start without credentials. The launcher keeps a random
    // password per install; the admin page is http://localhost:11779 with user "admin".
    const env = { ...process.env, CYBERPUNKMP_ADMIN_USERNAME: 'admin', CYBERPUNKMP_ADMIN_PASSWORD: adminPassword }
    const child = spawn(cmd, args, { cwd: serverDir(), windowsHide: true, env })
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
    log(`Your router's internet address (${externalIp}) is itself private (second router or CGNAT). Steam invites still work; direct-address joins will not.`)
  }

  const publicIp = webIp || (externalIp && !upnp.isPrivateAddress(externalIp) ? externalIp : null)
  if (!mapping) {
    log(`No router port opened. Steam invites do not need one (they go through the Steam relay); forward UDP ${port} only for friends joining by address.`)
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

export async function startHosting ({ name, maxPlayers, gamePath, adminPassword }) {
  if (server) return status

  setStatus({ state: 'starting', lines: [], publicAddress: null, port: null, error: null })
  try {
    await writeServerConfig({ name, maxPlayers })
    log('Starting the server...')
    server = await startServerProcess(adminPassword)
    const port = server.port
    setStatus({ port })

    const publicIp = await openToInternet(port)

    // The server can stop while the router is being set up (it logs why); do not carry on as if it were running.
    if (!server) throw new Error('The server stopped while starting: see its message in the log.')

    const publicAddress = publicIp ? `${publicIp}:${port}` : null
    setStatus({ state: 'running', publicAddress })
    log(`Server admin page: http://localhost:11779 (user admin, password ${adminPassword})`)
    log(publicAddress
      ? `Friends join through Steam invites (Steam relay, no port forwarding). Direct address for non-Steam friends: ${publicAddress}`
      : 'Friends join through Steam invites (Steam relay, no port forwarding).')

    if (gamePath) {
      const how = await launchGame(gamePath, { host: true, ip: '127.0.0.1', port, publicAddress })
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
