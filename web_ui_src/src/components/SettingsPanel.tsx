import { useState } from 'react'

export function SettingsPanel() {
  const [settings, setSettings] = useState({
    printerName: 'Ender 3 S1',
    apiKey: 'changeme123',
    wifiMode: 'ap',
    wifiSsid: '',
    wifiPassword: '',
    apSsid: 'Ender3-Controller',
    apPassword: 'ender3controller',
    autoConnect: true,
    statusInterval: 2000,
  })

  const [saved, setSaved] = useState(false)

  const handleSave = () => {
    // In real implementation, send to API
    console.log('Saving settings:', settings)
    setSaved(true)
    setTimeout(() => setSaved(false), 3000)
  }

  return (
    <div className="flex-1 overflow-auto p-6" style={{ maxWidth: 800, margin: '0 auto' }}>
      <div style={{ marginBottom: 24 }}>
        <h2 style={{ fontSize: 24, fontWeight: 700, marginBottom: 4 }}>Settings</h2>
        <p className="text-secondary">Configure your printer controller</p>
      </div>

      {/* Printer Settings */}
      <div className="card setting-group">
        <h3 className="setting-label">Printer</h3>
        <div className="setting-row">
          <div className="setting-info">
            <div className="setting-title">Printer Name</div>
            <div className="setting-description">Displayed in the UI and network</div>
          </div>
          <input 
            className="input" 
            style={{ width: 250 }}
            value={settings.printerName}
            onChange={e => setSettings({...settings, printerName: e.target.value})}
          />
        </div>
      </div>

      {/* Authentication */}
      <div className="card setting-group">
        <h3 className="setting-label">Authentication</h3>
        <div className="setting-row">
          <div className="setting-info">
            <div className="setting-title">API Key</div>
            <div className="setting-description">Used for API authentication (Bearer token)</div>
          </div>
          <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
            <input 
              className="input" 
              type="password"
              style={{ width: 250 }}
              value={settings.apiKey}
              onChange={e => setSettings({...settings, apiKey: e.target.value})}
            />
            <button className="btn btn-ghost btn-sm" onClick={() => navigator.clipboard.writeText(settings.apiKey)}>
              Copy
            </button>
          </div>
        </div>
      </div>

      {/* Network Settings */}
      <div className="card setting-group">
        <h3 className="setting-label">Network</h3>
        
        <div className="setting-row">
          <div className="setting-info">
            <div className="setting-title">WiFi Mode</div>
            <div className="setting-description">Access Point (AP) or Station (connect to router)</div>
          </div>
          <select 
            className="input" 
            style={{ width: 200 }}
            value={settings.wifiMode}
            onChange={e => setSettings({...settings, wifiMode: e.target.value})}
          >
            <option value="ap">Access Point (Hotspot)</option>
            <option value="sta">Station (Connect to WiFi)</option>
          </select>
        </div>

        {settings.wifiMode === 'ap' && (
          <>
            <div className="setting-row">
              <div className="setting-info">
                <div className="setting-title">AP SSID</div>
                <div className="setting-description">Hotspot network name</div>
              </div>
              <input 
                className="input" 
                style={{ width: 250 }}
                value={settings.apSsid}
                onChange={e => setSettings({...settings, apSsid: e.target.value})}
              />
            </div>
            <div className="setting-row">
              <div className="setting-info">
                <div className="setting-title">AP Password</div>
                <div className="setting-description">Min 8 characters for WPA2</div>
              </div>
              <input 
                className="input" 
                type="password"
                style={{ width: 250 }}
                value={settings.apPassword}
                onChange={e => setSettings({...settings, apPassword: e.target.value})}
              />
            </div>
          </>
        )}

        {settings.wifiMode === 'sta' && (
          <>
            <div className="setting-row">
              <div className="setting-info">
                <div className="setting-title">WiFi SSID</div>
                <div className="setting-description">Router network name</div>
              </div>
              <input 
                className="input" 
                style={{ width: 250 }}
                value={settings.wifiSsid}
                onChange={e => setSettings({...settings, wifiSsid: e.target.value})}
              />
            </div>
            <div className="setting-row">
              <div className="setting-info">
                <div className="setting-title">WiFi Password</div>
                <div className="setting-description">Router password</div>
              </div>
              <input 
                className="input" 
                type="password"
                style={{ width: 250 }}
                value={settings.wifiPassword}
                onChange={e => setSettings({...settings, wifiPassword: e.target.value})}
              />
            </div>
          </>
        )}
      </div>

      {/* Connection Settings */}
      <div className="card setting-group">
        <h3 className="setting-label">Connection</h3>
        <div className="setting-row">
          <div className="setting-info">
            <div className="setting-title">Auto Connect</div>
            <div className="setting-description">Automatically connect to printer on startup</div>
          </div>
          <label style={{ display: 'flex', alignItems: 'center', gap: 8, cursor: 'pointer' }}>
            <input 
              type="checkbox" 
              checked={settings.autoConnect}
              onChange={e => setSettings({...settings, autoConnect: e.target.checked})}
            />
          </label>
        </div>
        <div className="setting-row">
          <div className="setting-info">
            <div className="setting-title">Status Update Interval</div>
            <div className="setting-description">How often to poll printer status (ms)</div>
          </div>
          <select 
            className="input" 
            style={{ width: 150 }}
            value={settings.statusInterval}
            onChange={e => setSettings({...settings, statusInterval: Number(e.target.value)})}
          >
            <option value={500}>500 ms</option>
            <option value={1000}>1 second</option>
            <option value={2000} selected>2 seconds</option>
            <option value={5000}>5 seconds</option>
          </select>
        </div>
      </div>

      {/* Save Button */}
      <div style={{ display: 'flex', justifyContent: 'flex-end', marginTop: 24 }}>
        <button className="btn btn-primary" onClick={handleSave} disabled={saved}>
          {saved ? '✓ Saved!' : 'Save Settings'}
        </button>
      </div>

      {/* Danger Zone */}
      <div className="card setting-group" style={{ borderColor: 'var(--accent-secondary)', marginTop: 24 }}>
        <h3 className="setting-label" style={{ color: 'var(--accent-secondary)' }}>Danger Zone</h3>
        <div className="setting-row">
          <div className="setting-info">
            <div className="setting-title" style={{ color: 'var(--accent-secondary)' }}>Restart ESP32</div>
            <div className="setting-description">Reboot the controller (will disconnect web UI)</div>
          </div>
          <button className="btn btn-danger" onClick={() => { if(confirm('Restart ESP32?')) { /* API call */ } }}>
            Restart
          </button>
        </div>
        <div className="setting-row">
          <div className="setting-info">
            <div className="setting-title" style={{ color: 'var(--accent-secondary)' }}>Factory Reset</div>
            <div className="setting-description">Erase all settings and LittleFS data</div>
          </div>
          <button className="btn btn-danger" onClick={() => { if(confirm('Factory reset? This cannot be undone!')) { /* API call */ } }}>
            Factory Reset
          </button>
        </div>
      </div>
    </div>
  )
}