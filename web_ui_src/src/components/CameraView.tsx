export function CameraView() {
  const [streaming, setStreaming] = useState(false)
  const [snapshot, setSnapshot] = useState<string | null>(null)

  return (
    <div className="flex-1 flex flex-col" style={{ minHeight: 0 }}>
      <div style={{ 
        padding: '16px 20px', borderBottom: '1px solid var(--border-color)',
        background: 'var(--bg-secondary)', display: 'flex', justifyContent: 'space-between', alignItems: 'center'
      }}>
        <h2 style={{ fontSize: 18, fontWeight: 600 }}>Camera</h2>
        <div style={{ display: 'flex', gap: 8 }}>
          <button className="btn btn-secondary btn-sm" onClick={() => setSnapshot('data:image/jpeg;base64,/9j/4AAQSkZJRgABAQEAYABgAAD/2wBDAAYEBQYFBAYGBQYHBwYIChAKCgkJChQODwwQFxQYGBcUFhYaHSUfGhsjHBYWICwgIyYnKSopGR8tMC0oMCUoKSj/2wBDAQcHBwoIChMKChMoGhYaKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCj/wAARCAABAAEDASIAAhEBAxEB/8QAFQABAQAAAAAAAAAAAAAAAAAAAAn/xAAUEAEAAAAAAAAAAAAAAAAAAAAA/8QAFB EBAAAAAAAAAAAAAAAAAAAAAP/EABQRAQAAAAAAAAAAAAAAAAAAAAD/2gAMAwEAAhEDEQA/AL+AB//Z')}>
            📸 Snapshot
          </button>
          <button className={`btn btn-sm ${streaming ? 'btn-danger' : 'btn-primary'}`} onClick={() => setStreaming(!streaming)}>
            {streaming ? '⏹ Stop Stream' : '▶ Start Stream'}
          </button>
        </div>
      </div>

      <div className="flex-1 camera-placeholder" style={{ flex: 1, position: 'relative' }}>
        {streaming ? (
          <div style={{ width: '100%', height: '100%', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center' }}>
            <div style={{ 
              width: '100%', height: '100%', maxWidth: 640, maxHeight: 480,
              background: '#0d0d1a', borderRadius: 'var(--radius-lg)',
              display: 'flex', alignItems: 'center', justifyContent: 'center'
            }}>
              <div style={{ textAlign: 'center', color: 'var(--text-muted)' }}>
                <div style={{ fontSize: 48, marginBottom: 16 }}>📹</div>
                <div>Streaming would appear here</div>
                <div className="text-secondary text-sm mt-2">MJPEG stream from ESP32-CAM or USB UVC</div>
              </div>
            </div>
            <div className="text-secondary text-sm mt-4">Stream URL: <code>http://IP:81/stream</code></div>
          </div>
        ) : (
          <div>
            <div className="camera-placeholder-icon">📷</div>
            <div className="empty-title">Camera Not Connected</div>
            <div className="empty-description">
              Supported cameras:
              <ul style={{ textAlign: 'left', maxWidth: 300, margin: '16px auto', paddingLeft: 20 }}>
                <li>ESP32-CAM (OV2640) via DVP</li>
                <li>USB UVC cameras (Logitech C270, etc.)</li>
                <li>Raspberry Pi Camera via CSI</li>
              </ul>
              Camera support will be added in a future update.
            </div>
          </div>
        )}
      </div>
    </div>
  )
}