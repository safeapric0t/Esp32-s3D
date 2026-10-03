import { PrinterStatus } from '../types/printer'

interface SidebarProps {
  activeTab: string
  onTabChange: (tab: string) => void
  status: PrinterStatus | null
  connected: boolean
}

const tabs = [
  { id: 'monitor', label: 'Monitor', icon: () => <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><rect x="2" y="3" width="20" height="14" rx="2"/><path d="M8 21h8"/><path d="M12 17v4"/></svg> },
  { id: 'files', label: 'Files', icon: () => <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg> },
  { id: 'control', label: 'Control', icon: () => <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><circle cx="12" cy="12" r="3"/><path d="M12 1v6"/><path d="M12 17v6"/><path d="M1 12h6"/><path d="M17 12h6"/></svg> },
  { id: 'console', label: 'Console', icon: () => <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><polyline points="4 17 10 11 4 5"/><line x1="12" y1="19" x2="20" y2="19"/></svg> },
  { id: 'camera', label: 'Camera', icon: () => <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><path d="M23 19a2 2 0 0 1-2 2H3a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h4l2-3h6l2 3h4a2 2 0 0 1 2 2z"/><circle cx="12" cy="12" r="4"/></svg> },
  { id: 'settings', label: 'Settings', icon: () => <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg> },
]

export function Sidebar({ activeTab, onTabChange, status, connected }: SidebarProps) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%', padding: '16px 12px' }}>
      {/* Logo/Brand */}
      <div style={{ 
        display: 'flex', alignItems: 'center', gap: 12, 
        padding: '12px', marginBottom: 8,
        background: 'var(--bg-tertiary)', borderRadius: 'var(--radius-lg)'
      }}>
        <div style={{ 
          width: 40, height: 40, borderRadius: 'var(--radius-md)',
          background: 'linear-gradient(135deg, var(--accent-primary), var(--accent-info))',
          display: 'flex', alignItems: 'center', justifyContent: 'center'
        }}>
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="white" strokeWidth="2.5">
            <path d="M12 2L2 7l10 5 10-5-10-5z"/>
            <path d="M2 17l10 5 10-5"/>
            <path d="M2 12l10 5 10-5"/>
          </svg>
        </div>
        <div>
          <div style={{ fontWeight: 700, fontSize: 16 }}>Ender 3 S1</div>
          <div style={{ fontSize: 11, color: 'var(--text-muted)' }}>ESP32-S3 Controller</div>
        </div>
      </div>

      {/* Navigation */}
      <nav style={{ flex: 1, display: 'flex', flexDirection: 'column', gap: 4 }}>
        {tabs.map(tab => (
          <button
            key={tab.id}
            onClick={() => onTabChange(tab.id as any)}
            style={{
              display: 'flex', alignItems: 'center', gap: 12,
              padding: '12px 14px',
              borderRadius: 'var(--radius-md)',
              background: activeTab === tab.id ? 'var(--accent-primary-dim)' : 'transparent',
              border: activeTab === tab.id ? '1px solid var(--accent-primary)' : 'none',
              color: activeTab === tab.id ? 'var(--accent-primary)' : 'var(--text-secondary)',
              fontWeight: activeTab === tab.id ? 600 : 500,
              fontSize: 14,
              width: '100%',
              transition: 'all var(--transition-fast)'
            }}
          >
            <span style={{ display: 'flex', alignItems: 'center' }}>{tab.icon()}</span>
            {tab.label}
          </button>
        ))}
      </nav>

      {/* Printer Quick Status */}
      <div style={{ 
        padding: '16px', 
        background: 'var(--bg-card)', 
        border: '1px solid var(--border-color)', 
        borderRadius: 'var(--radius-lg)',
        marginTop: 'auto'
      }}>
        <div style={{ 
          display: 'flex', alignItems: 'center', justifyContent: 'space-between',
          marginBottom: 12
        }}>
          <span className="text-secondary text-sm">Quick Stats</span>
          <span style={{
            width: 8, height: 8, borderRadius: '50%',
            background: connected ? 'var(--accent-primary)' : 'var(--state-disconnected)'
          }}></span>
        </div>

        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: 12 }}>
          <div style={{ textAlign: 'center', padding: '12px', background: 'var(--bg-tertiary)', borderRadius: 'var(--radius-md)' }}>
            <div style={{ fontSize: 20, fontWeight: 700, color: 'var(--accent-primary)' }}>
              {status?.hotend.current.toFixed(0) || 0}°
            </div>
            <div style={{ fontSize: 11, color: 'var(--text-muted)' }}>Hotend</div>
          </div>
          <div style={{ textAlign: 'center', padding: '12px', background: 'var(--bg-tertiary)', borderRadius: 'var(--radius-md)' }}>
            <div style={{ fontSize: 20, fontWeight: 700, color: 'var(--accent-info)' }}>
              {status?.bed.current.toFixed(0) || 0}°
            </div>
            <div style={{ fontSize: 11, color: 'var(--text-muted)' }}>Bed</div>
          </div>
          <div style={{ textAlign: 'center', padding: '12px', background: 'var(--bg-tertiary)', borderRadius: 'var(--radius-md)' }}>
            <div style={{ fontSize: 20, fontWeight: 700, color: 'var(--text-primary)' }}>
              {status?.progress.progress.toFixed(0) || 0}%
            </div>
            <div style={{ fontSize: 11, color: 'var(--text-muted)' }}>Progress</div>
          </div>
          <div style={{ textAlign: 'center', padding: '12px', background: 'var(--bg-tertiary)', borderRadius: 'var(--radius-md)' }}>
            <div style={{ fontSize: 20, fontWeight: 700, color: 'var(--text-primary)' }}>
              {status?.progress.layer_current || 0}
            </div>
            <div style={{ fontSize: 11, color: 'var(--text-muted)' }}>Layer</div>
          </div>
        </div>
      </div>
    </div>
  )
}