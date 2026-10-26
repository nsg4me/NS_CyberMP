// Update check against the repo's GitHub Releases, and in-place update of this install.
//
// The release has CyberpunkMP-windows.zip and CyberpunkMP-linux.tar.gz, each holding a CyberpunkMP/ folder that is
// this install's layout (launcher, mod/, server/). Files cannot be replaced while the launcher runs (Windows locks
// them), so a small script waits for the launcher to exit, copies the new files over, and starts it again.
import { spawn } from 'child_process'
import { promises as fs, existsSync } from 'fs'
import os from 'os'
import path from 'path'
import decompress from 'decompress'
import electron from 'electron'

const REPO = 'nsg4me/NS_CyberMP'
const ASSET = process.platform === 'win32' ? 'CyberpunkMP-windows.zip' : 'CyberpunkMP-linux.tar.gz'

export function currentVersion () {
  return electron.app.getVersion()
}

// Semver-ish compare: 0.2.0 > 0.2.0-dev.41 > 0.1.9
function parse (version) {
  const [core, pre] = String(version).replace(/^v/, '').split('-', 2)
  return { nums: core.split('.').map((n) => parseInt(n, 10) || 0), pre: pre ?? null }
}

export function isNewer (candidate, current) {
  const a = parse(candidate)
  const b = parse(current)
  for (let i = 0; i < 3; i++) {
    if ((a.nums[i] ?? 0) !== (b.nums[i] ?? 0)) return (a.nums[i] ?? 0) > (b.nums[i] ?? 0)
  }
  if (a.pre === b.pre) return false
  if (a.pre === null) return true // release beats its own pre-release/dev builds
  if (b.pre === null) return false
  const pa = a.pre.split('.')
  const pb = b.pre.split('.')
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const x = pa[i]
    const y = pb[i]
    if (x === undefined) return false
    if (y === undefined) return true
    const nx = Number(x)
    const ny = Number(y)
    if (!Number.isNaN(nx) && !Number.isNaN(ny)) {
      if (nx !== ny) return nx > ny
    } else if (x !== y) {
      return x > y
    }
  }
  return false
}

export async function check () {
  const current = currentVersion()
  const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, {
    headers: { 'User-Agent': 'NS-CyberMP-launcher', Accept: 'application/vnd.github+json' }
  })
  if (res.status === 404) return { current, available: false } // no release published yet
  if (!res.ok) throw new Error(`update check failed (${res.status})`)

  const release = await res.json()
  const latest = String(release.tag_name || '').replace(/^v/, '')
  const asset = (release.assets || []).find((a) => a.name === ASSET)

  return {
    current,
    latest,
    available: !!asset && isNewer(latest, current),
    url: asset?.browser_download_url ?? null,
    size: asset?.size ?? 0,
    page: release.html_url,
    notes: release.body ?? ''
  }
}

async function download (url, file, onProgress) {
  const res = await fetch(url, { headers: { 'User-Agent': 'NS-CyberMP-launcher' }, redirect: 'follow' })
  if (!res.ok || !res.body) throw new Error(`download failed (${res.status})`)

  const total = Number(res.headers.get('content-length')) || 0
  const handle = await fs.open(file, 'w')
  let received = 0
  try {
    for await (const chunk of res.body) {
      await handle.write(chunk)
      received += chunk.length
      if (total) onProgress(Math.round((received / total) * 100))
    }
  } finally {
    await handle.close()
  }
}

// Downloads and unpacks the update, then hands over to a script that swaps the files once the launcher has quit.
export async function install (installRoot, onProgress, onLog) {
  const info = await check()
  if (!info.available) throw new Error('No newer version is available.')

  const temp = await fs.mkdtemp(path.join(os.tmpdir(), 'cpmp-update-'))
  const archive = path.join(temp, ASSET)

  onLog(`Downloading ${info.latest}...`)
  await download(info.url, archive, onProgress)

  onLog('Unpacking...')
  const unpacked = path.join(temp, 'x')
  await decompress(archive, unpacked)
  const source = existsSync(path.join(unpacked, 'CyberpunkMP')) ? path.join(unpacked, 'CyberpunkMP') : unpacked

  const exe = process.execPath
  if (process.platform === 'win32') {
    const script = path.join(temp, 'update.cmd')
    await fs.writeFile(script, [
      '@echo off',
      ':wait',
      `tasklist /FI "PID eq ${process.pid}" | find "${process.pid}" >nul && (timeout /t 1 /nobreak >nul & goto wait)`,
      `robocopy "${source}" "${installRoot}" /E /R:5 /W:1 /NFL /NDL /NJH /NJS /NP >nul`,
      `start "" "${exe}"`,
      `rmdir /s /q "${temp}"`
    ].join('\r\n'))
    spawn('cmd.exe', ['/c', script], { detached: true, stdio: 'ignore', windowsHide: true }).unref()
  } else {
    const script = path.join(temp, 'update.sh')
    await fs.writeFile(script, [
      '#!/bin/sh',
      `while kill -0 ${process.pid} 2>/dev/null; do sleep 0.5; done`,
      `cp -rf "${source}/." "${installRoot}/"`,
      `chmod +x "${exe}" "${installRoot}/server/Server.Loader" 2>/dev/null`,
      `rm -rf "${temp}"`,
      `"${exe}" >/dev/null 2>&1 &`
    ].join('\n'))
    await fs.chmod(script, 0o755)
    spawn('/bin/sh', [script], { detached: true, stdio: 'ignore' }).unref()
  }

  onLog(`Restarting into ${info.latest}...`)
  setTimeout(() => electron.app.quit(), 500)
  return info
}
