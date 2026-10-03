import { useRef, useEffect } from 'react'

interface ConsolePanelProps {
  logs: string[]
  onClear: () => void
  onSend: (command: string) => void
}

export function ConsolePanel({ logs, onClear, onSend }: ConsolePanelProps) {
  const [input, setInput] = useState('')
  const [autoScroll, setAutoScroll] = useState(true)
  const logsEndRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (autoScroll) {
      logsEndRef.current?.scrollIntoView({ behavior: 'smooth' })
    }
  }, [logs, autoScroll])

  const handleSend = (e: React.FormEvent) => {
    e.preventDefault()
    if (input.trim()) {
      onSend(input.trim())
      setInput('')
    }
  }

  const getLogClass = (log: string) => {
    if (log.includes('Error') || log.includes('error') || log.includes('!!')) return 'console-error'
    if (log.startsWith('>') || log.includes('Send:')) return 'console-sent'
    if (log.startsWith('<') || log.includes('Recv:')) return 'console-received'
    if (log.includes('System') || log.includes('ok')) return 'console-system'
    return ''
  }

  return (
    <div className="flex-1 flex flex-col" style={{ minHeight: 0 }}>
      <div style={{ 
        padding: '16px 20px', borderBottom: '1px solid var(--border-color)',
        background: 'var(--bg-secondary)', display: 'flex', justifyContent: 'space-between', alignItems: 'center'
      }}>
        <h2 style={{ fontSize: 18, fontWeight: 600 }}>G-Code Console</h2>
        <div style={{ display: 'flex', alignItems: 'center', gap: 12 }}>
          <label style={{ display: 'flex', alignItems: 'center', gap: 6, fontSize: 13, color: 'var(--text-secondary)' }}>
            <input type="checkbox" checked={autoScroll} onChange={e => setAutoScroll(e.target.checked)} />
            Auto-scroll
          </label>
          <button className="btn btn-ghost btn-sm" onClick={onClear}>Clear</button>
        </div>
      </div>

      <div className="console flex-1 flex flex-col" style={{ overflow: 'hidden', minHeight: 0 }}>
        <div className="flex-1 overflow-y-auto p-4" style={{ minHeight: 0 }}>
          {logs.map((log, i) => (
            <div key={i} className={`console-line ${getLogClass(log)}`}>
              {log}
            </div>
          ))}
          <div ref={logsEndRef} />
        </div>

        <form onSubmit={handleSend} style={{ padding: 16, borderTop: '1px solid var(--border-color)', background: 'var(--bg-secondary)' }}>
          <div style={{ display: 'flex', gap: 8 }}>
            <input
              type="text"
              className="input flex-1 font-mono"
              value={input}
              onChange={e => setInput(e.target.value)}
              placeholder="Enter G-code command (M114, G28 X0 Y0, etc.)"
              style={{ fontSize: 13 }}
            />
            <button className="btn btn-primary" type="submit">Send</button>
          </div>
        </form>
      </div>
    </div>
  )
}