import { useEffect, useRef, useState } from 'react'
import { Alert, Box, Button, Paper, Stack, TextField, Typography } from '@mui/material'
import { ContentCopy, Groups, PlayArrow, Stop } from '@mui/icons-material'

interface HostStatus {
  state: 'stopped' | 'starting' | 'running'
  lines: string[]
  port?: number | null
  publicAddress?: string | null
  error?: string | null
}

// Host a private server on this PC and let Steam friends join through invites.
export default function HostPanel () {
  const [status, setStatus] = useState<HostStatus>({ state: 'stopped', lines: [] })
  const [name, setName] = useState('CyberpunkMP')
  const [notice, setNotice] = useState<string | null>(null)
  const logRef = useRef<HTMLPreElement>(null)

  useEffect(() => {
    window.electronAPI.hostStatus().then(setStatus)
    return window.electronAPI.onHostStatus(setStatus)
  }, [])

  useEffect(() => {
    logRef.current?.scrollTo(0, logRef.current.scrollHeight)
  }, [status.lines])

  const start = async () => {
    setNotice(null)
    try {
      await window.electronAPI.hostStart({ name, maxPlayers: 4 })
    } catch (err: any) {
      // Errors from the main process arrive as "Error invoking remote method 'host-start': Error: ..."
      setNotice(String(err?.message ?? err).replace(/^Error invoking remote method '[^']+': (Error: )?/, ''))
    }
  }

  const joinViaSteam = async () => {
    try {
      await window.electronAPI.launchGame({ waitForInvite: true })
      setNotice('Game starting. Accept your friend\'s Steam invite (or use "Join Game" on them in your friends list) once you are in.')
    } catch (err: any) {
      setNotice(String(err?.message ?? err).replace(/^Error invoking remote method '[^']+': (Error: )?/, ''))
    }
  }

  const running = status.state === 'running'
  const busy = status.state === 'starting'

  return (
    <Paper sx={{ p: 2, mb: 2 }}>
      <Typography variant="h5" sx={{ mb: 1 }}>Play with friends</Typography>

      <Stack direction="row" spacing={2} alignItems="center">
        <TextField label="Server name" size="small" value={name} disabled={running || busy}
                   onChange={(e) => setName(e.target.value)} sx={{ maxWidth: 240 }}/>
        {running || busy
          ? <Button variant="contained" color="error" startIcon={<Stop/>} disabled={busy}
                    onClick={() => window.electronAPI.hostStop()}>Stop hosting</Button>
          : <Button variant="contained" startIcon={<PlayArrow/>} onClick={start}>Host game</Button>}
        <Button variant="outlined" startIcon={<Groups/>} onClick={joinViaSteam} disabled={running || busy}>
          Join a friend
        </Button>
      </Stack>

      {status.error && <Alert severity="error" sx={{ mt: 2 }}>{status.error}</Alert>}
      {notice && <Alert severity="info" sx={{ mt: 2 }}>{notice}</Alert>}

      {running && (
        <Alert severity="success" sx={{ mt: 2 }}>
          Hosting on port {status.port}. Invite friends from the Steam overlay in game:
          Shift+Tab → Friends → right-click a friend → <b>Invite to Game</b>.
          {status.publicAddress && (
            <Box sx={{ mt: 1 }}>
              Friends not on Steam can join with <b>{status.publicAddress}</b>{' '}
              <Button size="small" startIcon={<ContentCopy/>}
                      onClick={() => navigator.clipboard.writeText(status.publicAddress ?? '')}>Copy</Button>
            </Box>
          )}
        </Alert>
      )}

      {status.lines.length > 0 && (
        <Box component="pre" ref={logRef}
             sx={{ mt: 2, mb: 0, maxHeight: 160, overflow: 'auto', fontSize: 12, bgcolor: 'rgba(0,0,0,0.3)', p: 1, borderRadius: 1 }}>
          {status.lines.join('\n')}
        </Box>
      )}
    </Paper>
  )
}
