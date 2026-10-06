import { PrinterStatus } from '../types/printer'
import { useState } from 'react';
interface ControlPanelProps {
  status: PrinterStatus | null
  onSendGcode: (command: string) => void
}

import { useState } from 'react';

  const [jogDistance, setJogDistance] = useState(1)
  const [hotendTemp, setHotendTemp] = useState(0)
  const [bedTemp, setBedTemp] = useState(0)
  const [fanSpeed, setFanSpeed] = useState(0)
  const [customGcode, setCustomGcode] = useState('')

  const handleMove = (axis: string, direction: number) => {
    const distance = jogDistance * direction
    const params: any = { feedrate: 3000 }
    params[axis.toLowerCase()] = distance
    onSendGcode(`G91\nG1 ${axis}${distance} F3000\nG90`)
  }

  const handleHome = (axes: string[]) => {
    onSendGcode(`G28 ${axes.map(a => a.toUpperCase()).join(' ')}`)
  }

  const handleSetTemp = () => {
    if (hotendTemp > 0) onSendGcode(`M104 S${hotendTemp}`)
    if (bedTemp > 0) onSendGcode(`M140 S${bedTemp}`)
  }

  const handleSetFan = () => {
    const pwm = Math.round(fanSpeed * 255 / 100)
    onSendGcode(`M106 S${pwm}`)
  }

  const handleSendGcode = () => {
    if (customGcode.trim()) {
      onSendGcode(customGcode.trim())
      setCustomGcode('')
    }
  }

  const preheatPresets = [
    { label: 'PLA', hotend: 200, bed: 60 },
    { label: 'PETG', hotend: 240, bed: 80 },
    { label: 'ABS', hotend: 250, bed: 100 },
    { label: 'TPU', hotend: 210, bed: 50 },
    { label: 'Cool Down', hotend: 0, bed: 0 },
  ]

  if (!status) {
    return (
      <div className="flex-1 flex items-center justify-center">
        <div className="text-secondary">Printer not connected</div>
      </div>
    )
  }

  return (
    <div className="flex-1 flex flex-col gap-4 overflow-auto p-4">
      {/* Temperature Controls */}
      <div className="card">
        <div className="card-header" style={{ display: 'flex', justifyContent: 'space-between' }}>
          <span>Temperature Control</span>
          <span className="badge badge-idle">{status.hotend.current.toFixed(1)}° / {status.hotend.target.toFixed(0)}°</span>
        </div>
        <div className="card-body">
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8, marginBottom: 16 }}>
            {preheatPresets.map(preset => (
              <button
                key={preset.label}
                className="btn btn-secondary btn-sm"
                onClick={() => { setHotendTemp(preset.hotend); setBedTemp(preset.bed); handleSetTemp() }}
              >
                {preset.label} ({preset.hotend}°/{preset.bed}°)
              </button>
            ))}
          </div>
          
          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 16 }}>
            <div>
              <label className="setting-label">Hotend Target</label>
              <div style={{ display: 'flex', gap: 8 }}>
                <input 
                  type="number" 
                  className="input" 
                  value={hotendTemp} 
                  onChange={e => setHotendTemp(Number(e.target.value))}
                  min={0} max={300} step={1}
                />
                <button className="btn btn-primary" onClick={handleSetTemp} style={{ flex: 1 }}>Set</button>
              </div>
            </div>
            <div>
              <label className="setting-label">Bed Target</label>
              <div style={{ display: 'flex', gap: 8 }}>
                <input 
                  type="number" 
                  className="input" 
                  value={bedTemp} 
                  onChange={e => setBedTemp(Number(e.target.value))}
                  min={0} max={120} step={1}
                />
                <button className="btn btn-primary" onClick={handleSetTemp} style={{ flex: 1 }}>Set</button>
              </div>
            </div>
          </div>

          <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 12, marginTop: 16 }}>
            <div style={{ padding: '12px', background: 'var(--bg-tertiary)', borderRadius: 'var(--radius-md)', textAlign: 'center' }}>
              <div style={{ fontSize: 11, color: 'var(--text-muted)' }}>Current Hotend</div>
              <div style={{ fontSize: 28, fontWeight: 700, color: 'var(--accent-primary)' }}>{status.hotend.current.toFixed(1)}°</div>
            </div>
            <div style={{ padding: '12px', background: 'var(--bg-tertiary)', borderRadius: 'var(--radius-md)', textAlign: 'center' }}>
              <div style={{ fontSize: 11, color: 'var(--text-muted)' }}>Current Bed</div>
              <div style={{ fontSize: 28, fontWeight: 700, color: 'var(--accent-info)' }}>{status.bed.current.toFixed(1)}°</div>
            </div>
          </div>
        </div>
      </div>

      {/* Fan Control */}
      <div className="card">
        <div className="card-header">Fan Control</div>
        <div className="card-body">
          <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
            <label className="setting-label" style={{ margin: 0, width: 80 }}>Part Cooling</label>
            <input 
              type="range" 
              min="0" max="100" 
              value={fanSpeed} 
              onChange={e => setFanSpeed(Number(e.target.value))}
              className="fan-slider flex-1"
            />
            <span style={{ width: 50, textAlign: 'right', fontWeight: 600, fontVariantNumeric: 'tabular-nums' }}>
              {fanSpeed}%
            </span>
            <button className="btn btn-primary" onClick={handleSetFan}>Set</button>
          </div>
        </div>
      </div>

      {/* Movement Controls */}
      <div className="card">
        <div className="card-header" style={{ display: 'flex', justifyContent: 'space-between' }}>
          <span>Movement</span>
        </div>
        <div className="card-body">
          {/* Jog Distance */}
          <div style={{ display: 'flex', alignItems: 'center', gap: 12, marginBottom: 16 }}>
            <label className="setting-label" style={{ margin: 0 }}>Jog Distance</label>
            <select 
              className="input" 
              style={{ width: 'auto' }}
              value={jogDistance} 
              onChange={e => setJogDistance(Number(e.target.value))}
            >
              <option value={0.1}>0.1 mm</option>
              <option value={1} selected>1 mm</option>
              <option value={10}>10 mm</option>
              <option value={50}>50 mm</option>
            </select>
          </div>

          {/* XY Jog */}
          <div style={{ marginBottom: 16 }}>
            <div className="setting-label" style={{ marginBottom: 8 }}>XY Movement</div>
            <div className="jog-grid">
              <button className="jog-btn" onMouseDown={() => handleMove('Y', 1)} onMouseUp={() => onSendGcode('')} onMouseLeave={() => onSendGcode('')}>⬆</button>
              <button className="jog-btn" onMouseDown={() => handleMove('X', -1)} onMouseUp={() => onSendGcode('')} onMouseLeave={() => onSendGcode('')}>⬅</button>
              <button className="jog-btn center" onClick={() => handleHome(['x', 'y'])}>🏠 XY</button>
              <button className="jog-btn" onMouseDown={() => handleMove('X', 1)} onMouseUp={() => onSendGcode('')} onMouseLeave={() => onSendGcode('')}>➡</button>
              <button className="jog-btn" onMouseDown={() => handleMove('Y', -1)} onMouseUp={() => onSendGcode('')} onMouseLeave={() => onSendGcode('')}>⬇</button>
            </div>
          </div>

          {/* Z Jog */}
          <div style={{ marginBottom: 16 }}>
            <div className="setting-label" style={{ marginBottom: 8 }}>Z Movement</div>
            <div style={{ display: 'flex', gap: 8 }}>
              <button className="btn btn-secondary flex-1" onMouseDown={() => handleMove('Z', 1)} onMouseUp={() => onSendGcode('')} onMouseLeave={() => onSendGcode('')}>Z +{jogDistance}mm</button>
              <button className="btn btn-secondary flex-1" onMouseDown={() => handleMove('Z', -1)} onMouseUp={() => onSendGcode('')} onMouseLeave={() => onSendGcode('')}>Z -{jogDistance}mm</button>
              <button className="btn btn-primary" onClick={() => handleHome(['z'])}>Home Z</button>
            </div>
          </div>

          {/* Extruder Jog */}
          <div style={{ marginBottom: 16 }}>
            <div className="setting-label" style={{ marginBottom: 8 }}>Extruder</div>
            <div style={{ display: 'flex', gap: 8 }}>
              <button className="btn btn-secondary flex-1" onMouseDown={() => handleMove('E', 5)} onMouseUp={() => onSendGcode('')} onMouseLeave={() => onSendGcode('')}>Extrude 5mm</button>
              <button className="btn btn-secondary flex-1" onMouseDown={() => handleMove('E', -5)} onMouseUp={() => onSendGcode('')} onMouseLeave={() => onSendGcode('')}>Retract 5mm</button>
            </div>
          </div>

          {/* Home All */}
          <button className="btn btn-danger" onClick={() => handleHome(['x', 'y', 'z'])}>Home All Axes</button>
        </div>
      </div>

      {/* Custom G-Code */}
      <div className="card flex-1 flex flex-col" style={{ minHeight: 0 }}>
        <div className="card-header" style={{ display: 'flex', justifyContent: 'space-between' }}>
          <span>Custom G-Code</span>
        </div>
        <div className="card-body flex-1 flex flex-col" style={{ minHeight: 0 }}>
          <textarea
            className="input flex-1 font-mono"
            style={{ resize: 'none', minHeight: 120, background: '#0d0d1a', fontSize: 12 }}
            value={customGcode}
            onChange={e => setCustomGcode(e.target.value)}
            placeholder="Enter G-code command (e.g., M114, M105, G28)..."
          />
          <button className="btn btn-primary mt-3" onClick={handleSendGcode} style={{ alignSelf: 'flex-end' }}>
            Send
          </button>
        </div>
      </div>
    </div>
  )
}