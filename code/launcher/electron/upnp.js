// Minimal UPnP IGD client: find the router, open/close a port, read its WAN address.
// No dependency on a UPnP package so it behaves the same on Windows and Linux.
import dgram from 'dgram'
import http from 'http'
import { URL } from 'url'

const SSDP_ADDRESS = '239.255.255.250'
const SSDP_PORT = 1900
const SERVICE_TYPES = [
  'urn:schemas-upnp-org:service:WANIPConnection:2',
  'urn:schemas-upnp-org:service:WANIPConnection:1',
  'urn:schemas-upnp-org:service:WANPPPConnection:1'
]

function discoverLocations (timeoutMs = 2500) {
  return new Promise((resolve) => {
    const socket = dgram.createSocket({ type: 'udp4', reuseAddr: true })
    const locations = new Set()
    const search = (st) => Buffer.from(
      'M-SEARCH * HTTP/1.1\r\n' +
      `HOST: ${SSDP_ADDRESS}:${SSDP_PORT}\r\n` +
      'MAN: "ssdp:discover"\r\n' +
      'MX: 2\r\n' +
      `ST: ${st}\r\n\r\n`)

    socket.on('message', (msg) => {
      const match = /^location:\s*(.+)$/im.exec(msg.toString())
      if (match) locations.add(match[1].trim())
    })
    socket.on('error', () => {})
    socket.bind(() => {
      for (const st of ['urn:schemas-upnp-org:device:InternetGatewayDevice:1', ...SERVICE_TYPES]) {
        socket.send(search(st), SSDP_PORT, SSDP_ADDRESS)
      }
    })
    setTimeout(() => {
      try { socket.close() } catch {}
      resolve([...locations])
    }, timeoutMs)
  })
}

function httpRequest (url, { method = 'GET', headers = {}, body } = {}) {
  return new Promise((resolve, reject) => {
    const req = http.request(url, { method, headers, timeout: 4000 }, (res) => {
      let data = ''
      res.on('data', (chunk) => { data += chunk })
      res.on('end', () => resolve({ status: res.statusCode, body: data }))
    })
    req.on('timeout', () => req.destroy(new Error('timeout')))
    req.on('error', reject)
    if (body) req.write(body)
    req.end()
  })
}

async function findService (location) {
  const { body } = await httpRequest(location)
  for (const type of SERVICE_TYPES) {
    const index = body.indexOf(`<serviceType>${type}</serviceType>`)
    if (index < 0) continue
    const rest = body.slice(index)
    const control = /<controlURL>([^<]+)<\/controlURL>/.exec(rest)
    if (control) return { type, controlUrl: new URL(control[1].trim(), location).toString() }
  }
  return null
}

async function soap (gateway, action, args = {}) {
  const params = Object.entries(args).map(([k, v]) => `<${k}>${v}</${k}>`).join('')
  const body = '<?xml version="1.0"?>' +
    '<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">' +
    `<s:Body><u:${action} xmlns:u="${gateway.type}">${params}</u:${action}></s:Body></s:Envelope>`
  const res = await httpRequest(gateway.controlUrl, {
    method: 'POST',
    headers: {
      'Content-Type': 'text/xml; charset="utf-8"',
      SOAPAction: `"${gateway.type}#${action}"`,
      'Content-Length': Buffer.byteLength(body)
    },
    body
  })
  if (res.status !== 200) {
    const code = /<errorCode>(\d+)<\/errorCode>/.exec(res.body)
    const text = /<errorDescription>([^<]+)<\/errorDescription>/.exec(res.body)
    throw new Error(`${action} refused by the router (${code ? code[1] : res.status}${text ? ' ' + text[1] : ''})`)
  }
  return res.body
}

// The local address the router sees us at: the interface used to reach it.
function localAddressFor (host) {
  return new Promise((resolve, reject) => {
    const socket = dgram.createSocket('udp4')
    socket.connect(9, host, () => {
      const { address } = socket.address()
      socket.close()
      resolve(address)
    })
    socket.on('error', reject)
  })
}

export async function findGateway () {
  for (const location of await discoverLocations()) {
    try {
      const service = await findService(location)
      if (service) return { ...service, host: new URL(location).hostname }
    } catch {}
  }
  return null
}

export async function getExternalIp (gateway) {
  const body = await soap(gateway, 'GetExternalIPAddress')
  const match = /<NewExternalIPAddress>([^<]*)<\/NewExternalIPAddress>/.exec(body)
  return match ? match[1] : null
}

export async function addPortMapping (gateway, port, protocol, description) {
  const internalClient = await localAddressFor(gateway.host)
  const args = {
    NewRemoteHost: '',
    NewExternalPort: port,
    NewProtocol: protocol,
    NewInternalPort: port,
    NewInternalClient: internalClient,
    NewEnabled: 1,
    NewPortMappingDescription: description,
    NewLeaseDuration: 0
  }
  try {
    await soap(gateway, 'AddPortMapping', args)
  } catch (err) {
    // Some routers only accept leased mappings (error 725: OnlyPermanentLeasesSupported is the opposite case).
    await soap(gateway, 'AddPortMapping', { ...args, NewLeaseDuration: 7200 })
  }
  return internalClient
}

export async function deletePortMapping (gateway, port, protocol) {
  await soap(gateway, 'DeletePortMapping', { NewRemoteHost: '', NewExternalPort: port, NewProtocol: protocol })
}

// Address ranges that mean the router itself is behind another NAT (CGNAT or a second router).
export function isPrivateAddress (ip) {
  const [a, b] = ip.split('.').map(Number)
  return a === 10 || a === 127 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) ||
    (a === 100 && b >= 64 && b <= 127) || (a === 169 && b === 254)
}
