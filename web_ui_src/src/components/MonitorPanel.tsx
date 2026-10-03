import { PrinterStatus, PrintProgress } from '../types/printer'

interface MonitorPanelProps {
  status: PrinterStatus | null
  progress: PrintProgress | null
  connected: boolean
  onPrintStart: (file: string, mode: 'stream' | 'sd') => void
}

export function MonitorPanel({ status, progress, connected, onPrintStart }: MonitorPanelProps) {
  if (!status) {
    return (
      <div className="flex-1 flex flex-col items-center justify-center">
        <div className="empty-state">
          <div className="empty-icon">📡</div>
          <div className="empty-title">Waiting for Printer Connection</div>
          <div className="empty-description">
            Connect the ESP32-S3 to your Ender 3 S1 via USB-C cable.
            The printer will appear here once connected.
          </div>
        </div>
      </div>
    )
  }

  const formatTime = (seconds: number) => {
    const h = Math.floor(seconds / 3600)
    const m = Math.floor((seconds % 3600) / 60)
    const s = seconds % 60
    return `${h.toString().padStart(2, '0')}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`
  }

  const stateColors: Record<string, string> = {
    idle: 'badge-idle',
    printing: 'badge-printing',
    paused: 'badge-paused',
    error: 'badge-error',
    disconnected: 'badge-disconnected',
    heating: 'badge-heating'
  }

  const stateLabels: Record<string, string> = {
    idle: 'Idle',
    printing: 'Printing',
    paused: 'Paused',
    error: 'Error',
    disconnected: 'Disconnected',
    heating: 'Heating'
  }

  return (
    <div className="flex-1 flex flex-col gap-4 overflow-auto p-4">
      {/* Top Row - Camera + Print Summary */}
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 420px', gap: 16, height: '40%' }}>
        {/* Camera View */}
        <div className="card" style={{ overflow: 'hidden', position: 'relative', minHeight: 0 }}>
          <div className="card-header" style={{ display: 'flex', justifyContent: 'space-between' }}>
            <span>Camera</span>
            <span className="badge badge-disconnected">Offline</span>
          </div>
          <div className="camera-placeholder" style={{ flex: 1 }}>
            <div className="camera-placeholder-icon">📷</div>
            <div className="empty-title">Camera Not Connected</div>
            <div className="empty-description">Connect ESP32-CAM or USB UVC camera for live view</div>
          </div>
        </div>

        {/* Print Summary */}
        <div className="card flex flex-col" style={{ minHeight: 0 }}>
          <div className="card-header" style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <span>Print Summary</span>
            <span className={`badge ${stateColors[status.state] || 'badge-idle'}`}>
              {stateLabels[status.state] || 'Unknown'}
            </span>
          </div>

          <div className="card-body flex-1 flex flex-col" style={{ minHeight: 0 }}>
            {progress && progress.filename && (
              <div style={{ marginBottom: 16 }}>
                <div style={{ 
                  fontWeight: 600, 
                  fontSize: 14, 
                  marginBottom: 4,
                  whiteSpace: 'nowrap',
                  overflow: 'hidden',
                  textOverflow: 'ellipsis'
                }}>
                  {progress.filename}
                </div>
                <div style={{ fontSize: 12, color: 'var(--text-muted)' }}>
                  {progress.file_size ? `${(progress.file_size / 1024 / 1024).toFixed(2)} MB` : ''}
                </div>
              </div>
            )}

            {/* Progress Bar */}
            <div style={{ marginBottom: 16 }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 8 }}>
                <span style={{ fontWeight: 600 }}>{progress?.progress?.toFixed(1) || 0}%</span>
                <span className="text-secondary text-sm">
                  Layer {progress?.layer_current || 0} / {progress?.layer_total || 0}
                </span>
              </div>
              <div className="progress-bar">
                <div 
                  className="progress-fill" 
                  style={{ width: `${progress?.progress || 0}%` }}
                ></div>
              </div>
            </div>

            {/* Time & Z Height */}
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: 12, marginBottom: 16 }}>
              <div style={{ padding: '12px', background: 'var(--bg-tertiary)', borderRadius: 'var(--radius-md)' }}>
                <div className="text-secondary text-xs" style={{ marginBottom: 4 }}>Elapsed</div>
                <div className="time-display time-elapsed" style={{ fontSize: 18, fontWeight: 600 }}>
                  {progress ? formatTime(progress.time_elapsed) : '00:00:00'}
                </div>
              </div>
              <div style={{ padding: '12px', background: 'var(--bg-tertiary)', borderRadius: 'var(--radius-md)' }}>
                <div className="text-secondary text-xs" style={{ marginBottom: 4 }}>Remaining</div>
                <div className="time-display time-remaining" style={{ fontSize: 18, fontWeight: 600 }}>
                  {progress ? formatTime(progress.time_remaining) : '--:--:--'}
                </div>
              </div>
              <div style={{ padding: '12px', background: 'var(--bg-tertiary)', borderRadius: 'var(--radius-md)' }}>
                <div className="text-secondary text-xs" style={{ marginBottom: 4 }}>Z Height</div>
                <div style={{ fontSize: 18, fontWeight: 600 }}>
                  {progress?.current_z?.toFixed(1) || 0} mm
                </div>
              </div>
              <div style={{ padding: '12px', background: 'var(--bg-tertiary)', borderRadius: 'var(--radius-md)' }}>
                <div className="text-secondary text-xs" style={{ marginBottom: 4 }}>Speed</div>
                <div style={{ fontSize: 18, fontWeight: 600, color: 'var(--accent-primary)' }}>100%</div>
              </div>
            </div>

            {/* Action Buttons */}
            <div style={{ display: 'flex', gap: 8, marginTop: 'auto' }}>
              <button 
                className="btn btn-primary flex-1"
                disabled={status.state === 'printing' || status.state === 'heating'}
                onClick={() => {}}
              >
                ▶ Start Print
              </button>
              <button 
                className="btn btn-secondary"
                disabled={status.state !== 'printing' && status.state !== 'paused'}
                onClick={() => {}}
              >
                {status.state === 'paused' ? '▶ Resume' : '⏸ Pause'}
              </button>
              <button 
                className="btn btn-danger"
                disabled={status.state !== 'printing' && status.state !== 'paused'}
                onClick={() => {}}
              >
                ⏹ Stop
              </button>
            </div>
          </div>
        </div>
      </div>

      {/* Bottom Row - Temperatures + Console + Controls */}
      <div style={{ display: 'grid', gridTemplateColumns: '300px 1fr 320px', gap: 16, height: '60%' }}>
        {/* Temperatures */}
        <div className="card flex flex-col" style={{ minHeight: 0 }}>
          <div className="card-header" style={{ display: 'flex', justifyContent: 'space-between' }}>
            <span>Temperatures</span>
            <button className="btn btn-ghost btn-sm">⚙ Preheat</button>
          </div>
          <div className="card-body flex-1" style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
            {/* Hotend */}
            <div>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 8 }}>
                <span className="text-secondary text-sm">Hotend</span>
                <span style={{ fontWeight: 600 }}>
                  {status.hotend.current.toFixed(1)}° / {status.hotend.target.toFixed(0)}°
                </span>
              </div>
              <div className="temp-bar">
                <div 
                  className="temp-bar-fill hotend" 
                  style={{ width: `${status.hotend.target > 0 ? (status.hotend.current / status.hotend.target * 100) : 0}%` }}
                ></div>
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: 4, fontSize: 12, color: 'var(--text-muted)' }}>
                <span>Power: {status.hotend.power.toFixed(0)}%</span>
              </div>
            </div>

            {/* Bed */}
            <div>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 8 }}>
                <span className="text-secondary text-sm">Bed</span>
                <span style={{ fontWeight: 600 }}>
                  {status.bed.current.toFixed(1)}° / {status.bed.target.toFixed(0)}°
                </span>
              </div>
              <div className="temp-bar">
                <div 
                  className="temp-bar-fill bed" 
                  style={{ width: `${status.bed.target > 0 ? (status.bed.current / status.bed.target * 100) : 0}%` }}
                ></div>
              </div>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginTop: 4, fontSize: 12, color: 'var(--text-muted)' }}>
                <span>Power: {status.bed.power.toFixed(0)}%</span>
              </div>
            </div>

            {/* Chamber (if available) */}
            {status.chamber.target > 0 && (
              <div>
                <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 8 }}>
                  <span className="text-secondary text-sm">Chamber</span>
                  <span style={{ fontWeight: 600 }}>
                    {status.chamber.current.toFixed(1)}° / {status.chamber.target.toFixed(0)}°
                  </span>
                </div>
                <div className="temp-bar">
                  <div 
                    className="temp-bar-fill" 
                    style={{ width: `${status.chamber.target > 0 ? (status.chamber.current / status.chamber.target * 100) : 0}%` }}
                  ></div>
                </div>
              </div>
            )}

            {/* Fans */}
            <div style={{ marginTop: 'auto', paddingTop: 16, borderTop: '1px solid var(--border-color)' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 8 }}>
                <span className="text-secondary text-sm">Fans</span>
              </div>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
                {status.fans.slice(0, 3).map((fan, i) => (
                  <div key={i} style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
                    <span style={{ width: 50, fontSize: 12, color: 'var(--text-secondary)' }}>
                      Fan {fan.id}
                    </span>
                    <input 
                      type="range" 
                      min="0" max="100" 
                      value={fan.speed}
                      className="fan-slider flex-1"
                    />
                    <span style={{ width: 40, textAlign: 'right', fontSize: 12, fontVariantNumeric: 'tabular-nums' }}>
                      {fan.speed}%
                    </span>
                  </div>
                ))}
              </div>
            </div>
          </div>
        </div>

        {/* G-Code Console */}
        <div className="card flex flex-col" style={{ minHeight: 0 }}>
          <div className="card-header" style={{ display: 'flex', justifyContent: 'space-between' }}>
            <span>G-Code Console</span>
            <button className="btn btn-ghost btn-sm">Clear</button>
          </div>
          <div className="console flex-1" style={{ overflow: 'auto', padding: 12, fontSize: 11 }}>
            <div className="console-line console-system">System ready. Connect printer to begin.</div>
            <div className="console-line console-received">ok</div>
          </div>
        </div>

        {/* Position & Quick Controls */}
        <div className="card flex flex-col" style={{ minHeight: 0 }}>
          <div className="card-header">Position</div>
          <div className="card-body flex-1" style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
            {/* Current Position */}
            <div style={{ display: 'grid', gridTemplateColumns: 'repeat(2, 1fr)', gap: 8 }}>
              {['X', 'Y', 'Z', 'E'].map((axis, i) => (
                <div key={axis} style={{ 
                  padding: '12px', background: 'var(--bg-tertiary)', borderRadius: 'var(--radius-md)',
                  textAlign: 'center'
                }}>
                  <div style={{ fontSize: 11, color: 'var(--text-muted)', textTransform: 'lowercase' }}>
                    {axis}
                  </div>
                  <div style={{ fontSize: 18, fontWeight: 700, fontVariantNumeric: 'tabular-nums' }}>
                    {status.position[axis.toLowerCase() as keyof typeof status.position]?.toFixed(2) || 0}
                  </div>
                </div>
              ))}
            </div>

            {/* Quick Move */}
            <div style={{ paddingTop: 8, borderTop: '1px solid var(--border-color)' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 12 }}>
                <span className="text-secondary text-sm">Jog Distance</span>
                <select className="input" style={{ width: 'auto', padding: '4px 8px', fontSize: 12 }}>
                  <option value="0.1">0.1 mm</option>
                  <option value="1" selected>1 mm</option>
                  <option value="10">10 mm</option>
                  <option value="50">50 mm</option>
                </select>
              </div>
              <div className="jog-grid">
                <button className="jog-btn" disabled>⬆</button>
                <button className="jog-btn" disabled>⬅</button>
                <button className="jog-btn center" disabled>🏠</button>
                <button className="jog-btn" disabled>➡</button>
                <button className="jog-btn" disabled>⬇</button>
                <button className="jog-btn" disabled>Z+</button>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  )
}