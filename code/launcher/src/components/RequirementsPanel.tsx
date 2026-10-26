import { useEffect, useRef, useState } from 'react'
import { Alert, Box, Button, Chip, Paper, Stack, Typography } from '@mui/material'
import { Download, Refresh } from '@mui/icons-material'

interface Item {
  id: string
  name: string
  wanted: string
  present: boolean
  version: string | null
  ok: boolean
}

interface Status {
  ready: boolean
  items: Item[]
  vcRedist?: boolean
  platform?: string
  error?: string
}

// Required mods (installed into the game folder from their GitHub releases) and the VC++ runtime.
export default function RequirementsPanel ({ gamePath }: { gamePath?: string }) {
  const [status, setStatus] = useState<Status | null>(null)
  const [busy, setBusy] = useState(false)
  const [lines, setLines] = useState<string[]>([])
  const [launchOption, setLaunchOption] = useState<{ checked: boolean, ok: boolean } | null>(null)
  const logRef = useRef<HTMLPreElement>(null)

  const refresh = () => {
    window.electronAPI.launchOptionStatus().then(setLaunchOption).catch(() => setLaunchOption(null))
    return window.electronAPI.requirementsStatus().then(setStatus)
  }

  useEffect(() => { refresh() }, [gamePath])

  useEffect(() => window.electronAPI.onRequirementsLog((line: string) => setLines((l) => [...l, line].slice(-200))), [])

  useEffect(() => {
    logRef.current?.scrollTo(0, logRef.current.scrollHeight)
  }, [lines])

  const install = async (force = false) => {
    setBusy(true)
    setLines([])
    try {
      setStatus(await window.electronAPI.requirementsInstall({ force }))
    } catch {
      refresh()
    } finally {
      setBusy(false)
    }
  }

  if (!status) return null

  const linux = status.platform === 'linux'

  return (
    <Paper sx={{ p: 2, mb: 2 }}>
      <Stack direction="row" alignItems="center" spacing={2} sx={{ mb: 1 }}>
        <Typography variant="h5" sx={{ flex: 1 }}>Requirements</Typography>
        <Button size="small" startIcon={<Refresh/>} onClick={refresh} disabled={busy}>Check</Button>
        <Button variant={status.ready ? 'outlined' : 'contained'} startIcon={<Download/>}
                onClick={() => install(false)} disabled={busy || !!status.error}>
          {busy ? 'Installing...' : status.ready ? 'Reinstall missing' : 'Install requirements'}
        </Button>
      </Stack>

      {status.error && <Alert severity="warning">{status.error}</Alert>}

      <Stack direction="row" spacing={1} useFlexGap flexWrap="wrap">
        {status.items.map((item) => (
          <Chip key={item.id}
                color={item.ok ? 'success' : 'warning'}
                variant={item.ok ? 'outlined' : 'filled'}
                label={`${item.name} ${item.ok ? (item.version ?? 'installed') : item.present ? `${item.version} → ${item.wanted}` : 'missing'}`}/>
        ))}
        {status.items.length > 0 && (
          <Chip color={status.vcRedist ? 'success' : 'default'} variant="outlined"
                label={status.vcRedist ? 'VC++ runtime installed' : 'VC++ runtime: installed with the button'}/>
        )}
      </Stack>

      {linux && (
        <Alert severity={launchOption?.checked && !launchOption.ok ? 'warning' : launchOption?.ok && launchOption.checked ? 'success' : 'info'}
               sx={{ mt: 2 }}
               action={<Button color="inherit" size="small"
                               onClick={() => navigator.clipboard.writeText('WINEDLLOVERRIDES="winmm=n,b" %command%')}>Copy</Button>}>
          {launchOption?.checked && launchOption.ok
            ? <>Steam launch option is set (RED4ext will load).</>
            : <>{launchOption?.checked ? <b>Steam launch option missing. </b> : null}
                In Steam, set Cyberpunk 2077's launch options to <b>WINEDLLOVERRIDES="winmm=n,b" %command%</b>,
                otherwise RED4ext does not load.</>}
        </Alert>
      )}

      {lines.length > 0 && (
        <Box component="pre" ref={logRef}
             sx={{ mt: 2, mb: 0, maxHeight: 140, overflow: 'auto', fontSize: 12, bgcolor: 'rgba(0,0,0,0.3)', p: 1, borderRadius: 1 }}>
          {lines.join('\n')}
        </Box>
      )}
    </Paper>
  )
}
