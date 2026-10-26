import { useEffect, useState } from 'react'
import { Alert, Box, Button, MenuItem, Paper, Select, Stack, Typography } from '@mui/material'
import { BugReport, Keyboard } from '@mui/icons-material'

interface Binding {
  id: string
  label: string
  key: string | null
}

const keyLabel = (key: string) => key.replace(/^IK_/, '').replace('NumPad', 'Num ')

// Key bindings for the multiplayer actions, and a one-click log bundle for troubleshooting.
export default function ToolsPanel () {
  const [bindings, setBindings] = useState<Binding[]>([])
  const [keys, setKeys] = useState<string[]>([])
  const [logResult, setLogResult] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    window.electronAPI.getBindings().then(setBindings).catch(() => setBindings([]))
    window.electronAPI.getBindingKeys().then(setKeys).catch(() => setKeys([]))
  }, [])

  const change = async (id: string, key: string) => {
    setError(null)
    const next = Object.fromEntries(bindings.map((b) => [b.id, b.id === id ? key : b.key]))
    try {
      setBindings(await window.electronAPI.setBindings(next))
    } catch (err: any) {
      setError(err?.message ?? String(err))
    }
  }

  const collect = async () => {
    setLogResult(null)
    setError(null)
    try {
      const result = await window.electronAPI.collectLogs()
      setLogResult(`Saved ${result.file} (${result.included.join(', ') || 'launcher info only'})`)
    } catch (err: any) {
      setError(err?.message ?? String(err))
    }
  }

  const used = new Map<string, number>()
  bindings.forEach((b) => { if (b.key) used.set(b.key, (used.get(b.key) ?? 0) + 1) })

  return (
    <Paper sx={{ p: 2, mb: 2 }}>
      <Stack direction="row" alignItems="center" spacing={2} sx={{ mb: 1 }}>
        <Typography variant="h5" sx={{ flex: 1 }}>Controls & support</Typography>
        <Button variant="outlined" startIcon={<BugReport/>} onClick={collect}>Collect logs</Button>
      </Stack>

      {bindings.length > 0 && (
        <>
          <Stack direction="row" alignItems="center" spacing={1} sx={{ mb: 1 }}>
            <Keyboard fontSize="small"/>
            <Typography variant="body2">Multiplayer keys (applied the next time the game starts)</Typography>
          </Stack>
          <Box sx={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fill, minmax(260px, 1fr))', gap: 1 }}>
            {bindings.map((binding) => (
              <Stack key={binding.id} direction="row" alignItems="center" spacing={1}>
                <Typography variant="body2" sx={{ flex: 1 }}>{binding.label}</Typography>
                <Select size="small" value={binding.key ?? ''} sx={{ minWidth: 110 }}
                        onChange={(e) => change(binding.id, e.target.value as string)}>
                  {keys.map((key) => <MenuItem key={key} value={key}>{keyLabel(key)}</MenuItem>)}
                </Select>
              </Stack>
            ))}
          </Box>
          {[...used.values()].some((n) => n > 1) && (
            <Typography variant="caption" color="warning.main">
              Two actions share a key. That is fine for connect/disconnect, but check it is what you want.
            </Typography>
          )}
        </>
      )}

      {logResult && <Alert severity="success" sx={{ mt: 2 }}>{logResult}. Send this file when something goes wrong.</Alert>}
      {error && <Alert severity="error" sx={{ mt: 2 }}>{error}</Alert>}
    </Paper>
  )
}
