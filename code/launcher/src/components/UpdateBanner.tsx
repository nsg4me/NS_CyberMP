import { useEffect, useState } from 'react'
import { Alert, Box, Button, LinearProgress, Stack, Typography } from '@mui/material'

interface UpdateInfo {
  current: string
  latest?: string
  available: boolean
  page?: string
}

// Checks the latest GitHub Release when the launcher opens and offers to update (or not).
export default function UpdateBanner () {
  const [info, setInfo] = useState<UpdateInfo | null>(null)
  const [dismissed, setDismissed] = useState(false)
  const [busy, setBusy] = useState(false)
  const [progress, setProgress] = useState(0)
  const [message, setMessage] = useState<string | null>(null)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    window.electronAPI.updateCheck().then(setInfo).catch(() => setInfo(null))
    const offProgress = window.electronAPI.onUpdateProgress(setProgress)
    const offLog = window.electronAPI.onUpdateLog(setMessage)
    return () => { offProgress(); offLog() }
  }, [])

  if (!info?.available || dismissed) return null

  const update = async () => {
    setBusy(true)
    setError(null)
    try {
      await window.electronAPI.updateInstall()
    } catch (err: any) {
      setError(err?.message ?? String(err))
      setBusy(false)
    }
  }

  return (
    <Alert severity="info" sx={{ mb: 2 }}
           action={!busy && (
             <Stack direction="row" spacing={1}>
               <Button color="inherit" size="small" variant="outlined" onClick={update}>Update</Button>
               <Button color="inherit" size="small" onClick={() => setDismissed(true)}>Later</Button>
             </Stack>
           )}>
      <Typography variant="body2">
        NS-CyberMP <b>{info.latest}</b> is available (you have {info.current}). Close the game before updating; the
        launcher restarts by itself.
      </Typography>
      {busy && (
        <Box sx={{ mt: 1 }}>
          <LinearProgress variant={progress ? 'determinate' : 'indeterminate'} value={progress}/>
          {message && <Typography variant="caption">{message}</Typography>}
        </Box>
      )}
      {error && <Typography variant="caption" color="error">{error}</Typography>}
    </Alert>
  )
}
