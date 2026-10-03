import { useState, useRef } from 'react'
import { FileInfo } from '../types/printer'

interface FileManagerProps {
  files: FileInfo[]
  loading: boolean
  onRefresh: () => void
  onUpload: (file: File) => Promise<boolean>
  onDelete: (name: string, onPrinterSd: boolean) => Promise<boolean>
  onCopyToPrinter: (name: string) => Promise<boolean>
  onPrint: (file: string, mode: 'stream' | 'sd') => void
}

export function FileManager({ 
  files, loading, onRefresh, onUpload, onDelete, onCopyToPrinter, onPrint 
}: FileManagerProps) {
  const [dragActive, setDragActive] = useState(false)
  const [uploading, setUploading] = useState(false)
  const fileInputRef = useRef<HTMLInputElement>(null)
  const [showPrintModal, setShowPrintModal] = useState<{file: string, onPrinterSd: boolean} | null>(null)

  const handleDrag = (e: React.DragEvent) => {
    e.preventDefault()
    e.stopPropagation()
    if (e.type === 'dragenter' || e.type === 'dragover') {
      setDragActive(true)
    } else if (e.type === 'dragleave') {
      setDragActive(false)
    }
  }

  const handleDrop = async (e: React.DragEvent) => {
    e.preventDefault()
    e.stopPropagation()
    setDragActive(false)

    if (e.dataTransfer.files.length > 0) {
      const file = e.dataTransfer.files[0]
      if (file.name.endsWith('.gcode') || file.name.endsWith('.g') || file.name.endsWith('.gco')) {
        setUploading(true)
        await onUpload(file)
        setUploading(false)
      }
    }
  }

  const handleFileSelect = async (e: React.ChangeEvent<HTMLInputElement>) => {
    if (e.target.files && e.target.files[0]) {
      const file = e.target.files[0]
      if (file.name.endsWith('.gcode') || file.name.endsWith('.g') || file.name.endsWith('.gco')) {
        setUploading(true)
        await onUpload(file)
        setUploading(false)
      }
      e.target.value = ''
    }
  }

  const formatSize = (bytes: number) => {
    if (bytes < 1024) return `${bytes} B`
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
    return `${(bytes / 1024 / 1024).toFixed(2)} MB`
  }

  const formatDate = (timestamp: number) => {
    return new Date(timestamp * 1000).toLocaleDateString(undefined, {
      year: 'numeric', month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit'
    })
  }

  return (
    <div 
      className="flex-1 flex flex-col"
      onDragEnter={handleDrag}
      onDragLeave={handleDrag}
      onDragOver={handleDrag}
      onDrop={handleDrop}
      style={{ background: dragActive ? 'var(--accent-primary-dim)' : 'transparent' }}
    >
      {/* Header */}
      <div style={{ 
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
        padding: '16px 20px', borderBottom: '1px solid var(--border-color)',
        background: 'var(--bg-secondary)'
      }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 16 }}>
          <h2 style={{ fontSize: 20, fontWeight: 600 }}>Files</h2>
          <span className="badge badge-idle">{files.length} files</span>
        </div>
        <div style={{ display: 'flex', gap: 8 }}>
          <button className="btn btn-ghost btn-icon" onClick={onRefresh} disabled={loading}>
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <polyline points="23 4 23 10 17 10"/><path d="M20.49 15a9 9 0 1 1-2.12-9.36L23 10"/>
            </svg>
          </button>
          <label className="btn btn-primary" style={{ cursor: 'pointer' }}>
            <input ref={fileInputRef} type="file" accept=".gcode,.g,.gco" onChange={handleFileSelect} style={{ display: 'none' }} />
            <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ marginRight: 8 }}>
              <path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/>
              <polyline points="17 8 12 3 7 8"/>
              <line x1="12" y1="3" x2="12" y2="15"/>
            </svg>
            Upload
          </label>
        </div>
      </div>

      {/* File List */}
      <div className="flex-1 overflow-auto" style={{ minHeight: 0 }}>
        {loading ? (
          <div style={{ display: 'flex', flexDirection: 'column', gap: 12, padding: 20 }}>
            {[1, 2, 3, 4, 5].map(i => (
              <div key={i} className="skeleton" style={{ height: 64, borderRadius: 'var(--radius-md)' }}></div>
            ))}
          </div>
        ) : files.length === 0 ? (
          <div className="flex-1 flex flex-col items-center justify-center">
            <div className="empty-state">
              <div className="empty-icon">📁</div>
              <div className="empty-title">No G-code Files</div>
              <div className="empty-description">
                Upload your first .gcode file to start printing.
                Drag and drop or click the upload button.
              </div>
              <label className="btn btn-primary mt-4" style={{ cursor: 'pointer' }}>
                <input ref={fileInputRef} type="file" accept=".gcode,.g,.gco" onChange={handleFileSelect} style={{ display: 'none' }} />
                Upload G-code File
              </label>
            </div>
          </div>
        ) : (
          <div className="file-list" style={{ padding: 16, gap: 8 }}>
            {files.map(file => (
              <div key={file.name} className="file-item">
                <div className="file-icon">
                  <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                    <path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/>
                    <polyline points="14 2 14 8 20 8"/>
                    <line x1="16" y1="13" x2="8" y2="13"/>
                    <line x1="16" y1="17" x2="8" y2="17"/>
                    <polyline points="10 9 9 9 8 9"/>
                  </svg>
                </div>
                <div className="file-info">
                  <div className="file-name">{file.name}</div>
                  <div className="file-meta">
                    <span>{formatSize(file.size)}</span>
                    <span>{formatDate(file.date)}</span>
                    <span className={`badge ${file.on_printer_sd ? 'badge-printing' : 'badge-idle'}`} style={{ fontSize: 10 }}>
                      {file.on_printer_sd ? 'Printer SD' : 'ESP32'}
                    </span>
                  </div>
                </div>
                <div className="file-actions">
                  {!file.on_printer_sd && (
                    <button 
                      className="btn btn-secondary btn-sm"
                      onClick={() => onCopyToPrinter(file.name)}
                      title="Copy to Printer SD"
                    >
                      📋 SD
                    </button>
                  )}
                  <button 
                    className="btn btn-primary btn-sm"
                    onClick={() => setShowPrintModal({ file: file.name, onPrinterSd: file.on_printer_sd })}
                    disabled={!file.on_printer_sd && false}
                  >
                    ▶ Print
                  </button>
                  <button 
                    className="btn btn-ghost btn-sm"
                    onClick={() => onDelete(file.name, file.on_printer_sd)}
                    title="Delete"
                  >
                    <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                      <polyline points="3 6 5 6 21 6"/>
                      <path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6m3 0V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>
                    </svg>
                  </button>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>

      {/* Print Modal */}
      {showPrintModal && (
        <div className="modal-overlay" onClick={() => setShowPrintModal(null)}>
          <div className="modal" onClick={e => e.stopPropagation()}>
            <div className="modal-header">
              <div className="modal-title">Start Print</div>
              <button className="modal-close" onClick={() => setShowPrintModal(null)}>
                <svg width="20" height="20" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
                  <line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>
                </svg>
              </button>
            </div>
            <div className="modal-body">
              <p>Print <strong>{showPrintModal.file}</strong>?</p>
              <div style={{ display: 'flex', flexDirection: 'column', gap: 12, marginTop: 16 }}>
                <label style={{ display: 'flex', alignItems: 'center', gap: 12, padding: 12, border: '1px solid var(--border-color)', borderRadius: 'var(--radius-md)', cursor: 'pointer' }}>
                  <input type="radio" name="mode" value="stream" defaultChecked />
                  <div>
                    <div style={{ fontWeight: 500 }}>Stream from ESP32</div>
                    <div className="text-secondary text-sm">Recommended. Pause/resume supported. No SD card wear.</div>
                  </div>
                </label>
                <label style={{ display: 'flex', alignItems: 'center', gap: 12, padding: 12, border: '1px solid var(--border-color)', borderRadius: 'var(--radius-md)', cursor: 'pointer' }}>
                  <input type="radio" name="mode" value="sd" />
                  <div>
                    <div style={{ fontWeight: 500 }}>Copy to Printer SD Card</div>
                    <div className="text-secondary text-sm">More reliable for long prints. Survives ESP32 restart.</div>
                  </div>
                </label>
              </div>
            </div>
            <div className="modal-footer">
              <button className="btn btn-secondary" onClick={() => setShowPrintModal(null)}>Cancel</button>
              <button 
                className="btn btn-primary" 
                onClick={() => { onPrint(showPrintModal.file, showPrintModal.onPrinterSd ? 'sd' : 'stream'); setShowPrintModal(null) }}
              >
                ▶ Start Print
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}