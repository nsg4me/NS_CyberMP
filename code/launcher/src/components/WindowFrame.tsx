import { AppBar, Box, Button, Toolbar } from '@mui/material'
import Typography from '@mui/material/Typography'
import CloseIcon from '@mui/icons-material/Close'
import MinimizeIcon from '@mui/icons-material/Minimize'
import ContentCopyIcon from '@mui/icons-material/ContentCopy'
import Crop75Icon from '@mui/icons-material/Crop75'
import { useState } from 'react'

export default function WindowFrame () {
  const [maximized, setMaximized] = useState(false)

  return (
    // The window has no system frame: this bar is where it is dragged from (buttons opt out so they stay clickable).
    <AppBar component="nav" sx={{ zIndex: 10000, WebkitAppRegion: 'drag' }}
            onDoubleClick={() => { window.electronAPI.maximizeApp(); setMaximized(m => !m) }}>
      <Toolbar variant="dense">
        <Typography
          fontSize="small"
          component="div"
          sx={{ flexGrow: 1, display: { xs: 'none', sm: 'block' } }}
        >
          NS-CyberMP
        </Typography>
        <Box sx={{ display: { xs: 'none', sm: 'block' }, WebkitAppRegion: 'no-drag' }}>
          <Button key={'minimize-app'} sx={{ color: '#fff' }} onClick={window.electronAPI.minimizeApp}>
            <MinimizeIcon fontSize="small"/>
          </Button>
          <Button key={'maximize-app'} sx={{ color: '#fff' }} onClick={() => {
            window.electronAPI.maximizeApp()
            setMaximized(lastMaximized => !lastMaximized)
          }}>
            {maximized ? <ContentCopyIcon fontSize="small" sx={{ transform: 'scaleX(-1)' }}/> : <Crop75Icon fontSize="small"/>}
          </Button>
          <Button key={'close-app'} sx={{ color: '#fff' }} onClick={window.electronAPI.closeApp}>
            <CloseIcon fontSize="small"/>
          </Button>
        </Box>
      </Toolbar>
    </AppBar>
  )
}
