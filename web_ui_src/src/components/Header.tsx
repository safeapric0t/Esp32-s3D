import { PrinterStatus } from '../types/printer'

interface HeaderProps {
  status: PrinterStatus | null
  connected: boolean
  onMenuClick: () => void
  onControlClick: () => void
  emergencyStop: boolean
}

export function Header({ status, connected, onMenuClick, onControlClick, emergencyStop }: HeaderProps) {
  const stateColors: Record<string, string> = {
    idle: 'var(--state-idle)',
    printing: 'var(--state-printing)',
    paused: 'var(--state-paused)',
    error: 'var(--state-error)',
    disconnected: 'var(--state-disconnected)',
    heating: 'var(--state-heating)'
  }

  const stateLabels: Record<string, string> = {
    idle: 'Idle',
    printing: 'Printing',
    paused: 'Paused',
    error: 'Error',
    disconnected: 'Disconnected',
    heating: 'Heating'
  }

  const currentState = status?.state || 'disconnected'
  const stateColor = stateColors[currentState] || 'var(--text-muted)'
  const stateLabel = stateLabels[currentState] || 'Unknown'

  return (
    <header style={{
      height: 'var(--header-height)',
      background: 'var(--bg-secondary)',
      borderBottom: '1px solid var(--border-color)',
      display: 'flex',
      alignItems: 'center',
      justifyContent: 'space-between',
      padding: '0 20px',
      position: 'relative',
      zIndex: 50
    }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 16 }}>
        <button onClick={onMenuClick} className="btn btn-ghost btn-icon" aria-label="Menu">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
            <line x1="3" y1="6" x2="21" y2="6"></line>
            <line x1="3" y1="12" x2="21" y2="12"></line>
            <line x1="3" y1="18" x2="21" y2="18"></line>
          </svg>
        </button>

        <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
          <div style={{ 
            width: 10, height: 10, borderRadius: '50%', 
            background: connected ? 'var(--accent-primary)' : 'var(--state-disconnected)',
            boxShadow: connected ? '0 0 8px var(--accent-primary)' : 'none'
          }}></div>
          <span style={{ fontWeight: 600, fontSize: 16 }}>Ender 3 S1</span>
        </div>

        <div style={{
          display: 'flex',
          alignItems: 'center',
          gap: 8,
          padding: '4px 12px',
          background: `${stateColor}22`,
          border: `1px solid ${stateColor}44`,
          borderRadius: 'var(--radius-full)',
          marginLeft: 16
        }}>
          <span style={{
            width: 8, height: 8, borderRadius: '50%',
            background: stateColor
          }}></span>
          <span style={{ 
            color: stateColor, 
            fontWeight: 600, 
            fontSize: 13,
            textTransform: 'capitalize'
          }}>
            {stateLabel}
          </span>
        </div>
      </div>

      <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        {/* Connection Status */}
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '0 12px', borderRight: '1px solid var(--border-color)' }}>
          <span className="text-secondary text-sm">USB</span>
          <span style={{
            width: 8, height: 8, borderRadius: '50%',
            background: status?.usb_connected ? 'var(--accent-primary)' : 'var(--state-disconnected)'
          }}></span>
        </div>

        {/* Emergency Stop */}
        <button 
          onClick={onControlClick} 
          className="btn btn-icon"
          style={{ background: emergencyStop ? 'var(--accent-secondary)' : 'var(--bg-tertiary)' }}
          aria-label={emergencyStop ? 'Emergency Stop Active' : 'Open Controls'}
        >
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
            <circle cx="12" cy="12" r="10"></circle>
            <line x1="12" y1="8" x2="12" y2="16"></line>
            <line x1="8" y1="12" x2="16" y2="12"></line>
          </svg>
        </button>
      </div>
    </header>
  )
}