import { useState, useEffect } from 'react'
import { useWebSocket } from './hooks/useWebSocket'
import { usePrinterStatus } from './hooks/usePrinterStatus'
import { useFiles } from './hooks/useFiles'
import { PrinterStatus, PrintProgress, FileInfo } from './types/printer'
import { Header } from './components/Header'
import { Sidebar } from './components/Sidebar'
import { MonitorPanel } from './components/MonitorPanel'
import { ControlPanel } from './components/ControlPanel'
import { FileManager } from './components/FileManager'
import { ConsolePanel } from './components/ConsolePanel'
import { CameraView } from './components/CameraView'
import { SettingsPanel } from './components/SettingsPanel'
import { ToastContainer, toast } from './components/Toast'
import { Modal } from './components/Modal'

type Tab = 'monitor' | 'files' | 'control' | 'console' | 'camera' | 'settings'

export function App() {
  const [activeTab, setActiveTab] = useState<Tab>('monitor')
  const [sidebarOpen, setSidebarOpen] = useState(false)
  const [controlPanelOpen, setControlPanelOpen] = useState(false)
  const [status, setStatus] = useState<PrinterStatus | null>(null)
  const [progress, setProgress] = useState<PrintProgress | null>(null)
  const [logs, setLogs] = useState<string[]>([])
  const [files, setFiles] = useState<FileInfo[]>([])
  const [selectedFile, setSelectedFile] = useState<FileInfo | null>(null)
  const [printConfirm, setPrintConfirm] = useState<{file: string, mode: 'stream' | 'sd'} | null>(null)
  const [emergencyStop, setEmergencyStop] = useState(false)

  const { connected, sendGcode } = useWebSocket({
    onStatus: setStatus,
    onProgress: setProgress,
    onLog: (level, message) => {
      setLogs(prev => [...prev.slice(-99), `[${new Date().toLocaleTimeString()}] [${level}] ${message}`])
    },
    onFileList: setFiles,
    onStateChange: (stateStr) => {
      toast.success(`Printer state: ${stateStr}`)
    }
  })

  const { status: polledStatus, progress: polledProgress, refresh: refreshStatus } = usePrinterStatus(5000)
  const { files: fetchedFiles, loading: filesLoading, upload, deleteFile, copyToPrinter, fetchFiles } = useFiles()

  // Merge WebSocket and polled status
  useEffect(() => {
    if (polledStatus && !status) setStatus(polledStatus)
    if (polledProgress && !progress) setProgress(polledProgress)
  }, [polledStatus, polledProgress, status, progress])

  const handlePrintStart = (file: string, mode: 'stream' | 'sd') => {
    setSelectedFile(files.find(f => f.name === file) || null)
    setPrintConfirm({ file, mode })
  }

  const confirmPrint = async () => {
    if (!printConfirm) return
    try {
      // Call API to start print
      const res = await fetch('/api/print/start', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer changeme123` },
        body: JSON.stringify({ file: printConfirm.file, mode: printConfirm.mode })
      })
      if (res.ok) {
        toast.success(`Print started: ${printConfirm.file}`)
        setActiveTab('monitor')
      } else {
        toast.error('Failed to start print')
      }
    } catch {
      toast.error('Failed to start print')
    }
    setPrintConfirm(null)
  }

  const handleEmergencyStop = async () => {
    setEmergencyStop(true)
    try {
      await fetch('/api/gcode', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json', 'Authorization': `Bearer changeme123` },
        body: JSON.stringify({ command: 'M112' })
      })
      toast.error('EMERGENCY STOP triggered!')
    } catch {
      toast.error('Failed to send emergency stop')
    }
    setTimeout(() => setEmergencyStop(false), 2000)
  }

  return (
    <div className="app-layout">
      {/* Sidebar */}
      <aside className={`sidebar ${sidebarOpen ? 'open' : ''}`}>
        <Sidebar 
          activeTab={activeTab} 
          onTabChange={setActiveTab}
          status={status}
          connected={connected}
        />
      </aside>

      {/* Main Content */}
      <main className="main-content">
        <Header 
          status={status}
          connected={connected}
          onMenuClick={() => setSidebarOpen(!sidebarOpen)}
          onControlClick={() => setControlPanelOpen(!controlPanelOpen)}
          emergencyStop={emergencyStop}
        />

        <div className="flex-1 flex overflow-hidden">
          {/* Content Area */}
          <div className="flex-1 flex flex-col overflow-hidden p-4">
            {activeTab === 'monitor' && (
              <MonitorPanel 
                status={status} 
                progress={progress}
                connected={connected}
                onPrintStart={handlePrintStart}
              />
            )}
            {activeTab === 'files' && (
              <FileManager 
                files={files}
                loading={filesLoading}
                onRefresh={fetchFiles}
                onUpload={upload}
                onDelete={deleteFile}
                onCopyToPrinter={copyToPrinter}
                onPrint={handlePrintStart}
              />
            )}
            {activeTab === 'control' && (
              <ControlPanel 
                status={status}
                onSendGcode={sendGcode}
              />
            )}
            {activeTab === 'console' && (
              <ConsolePanel logs={logs} onClear={() => setLogs([])} onSend={sendGcode} />
            )}
            {activeTab === 'camera' && <CameraView />}
            {activeTab === 'settings' && <SettingsPanel />}
          </div>

          {/* Control Panel (Right Sidebar) */}
          <aside className={`control-panel ${controlPanelOpen ? 'open' : ''}`}>
            <ControlPanel 
              status={status}
              onSendGcode={sendGcode}
            />
          </aside>
        </div>
      </main>

      {/* Modals */}
      {printConfirm && (
        <Modal
          title="Start Print"
          onClose={() => setPrintConfirm(null)}
          onConfirm={confirmPrint}
        >
          <p>Start printing <strong>{printConfirm.file}</strong>?</p>
          <p className="text-secondary text-sm mt-2">
            Mode: {printConfirm.mode === 'stream' ? 'Stream from ESP32 (recommended)' : 'Copy to printer SD card'}
          </p>
        </Modal>
      )}

      <ToastContainer />
    </div>
  )
}