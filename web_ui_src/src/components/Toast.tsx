import { createContext, useContext, useState, ReactNode } from 'react'

interface Toast {
  id: number
  type: 'success' | 'error' | 'warning' | 'info'
  message: string
}

interface ToastContextType {
  toast: (type: Toast['type'], message: string) => void
  success: (message: string) => void
  error: (message: string) => void
  warning: (message: string) => void
  info: (message: string) => void
}

const ToastContext = createContext<ToastContextType | null>(null)

export function ToastProvider({ children }: { children: ReactNode }) {
  const [toasts, setToasts] = useState<Toast[]>([])

  const addToast = (type: Toast['type'], message: string) => {
    const id = Date.now()
    setToasts(prev => [...prev, { id, type, message }])
    setTimeout(() => {
      setToasts(prev => prev.filter(t => t.id !== id))
    }, 5000)
  }

  const value = {
    toast: addToast,
    success: (msg: string) => addToast('success', msg),
    error: (msg: string) => addToast('error', msg),
    warning: (msg: string) => addToast('warning', msg),
    info: (msg: string) => addToast('info', msg),
  }

  return (
    <ToastContext.Provider value={value}>
      {children}
      <ToastContainer toasts={toasts} onRemove={id => setToasts(prev => prev.filter(t => t.id !== id))} />
    </ToastContext.Provider>
  )
}

export function useToast() {
  const context = useContext(ToastContext)
  if (!context) throw new Error('useToast must be used within ToastProvider')
  return context
}

interface ToastContainerProps {
  toasts: Toast[]
  onRemove: (id: number) => void
}

export { ToastContainer };

  return (
    <div className="toast-container" style={{ pointerEvents: 'none' }}>
      {toasts.map(t => (
        <div 
          key={t.id} 
          className={`toast toast-${t.type}`}
          style={{ pointerEvents: 'auto' }}
          onClick={() => onRemove(t.id)}
        >
          <div style={{ flex: 1 }}>{t.message}</div>
          <button className="btn btn-ghost btn-icon btn-sm" onClick={e => { e.stopPropagation(); onRemove(t.id) }}>
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2">
              <line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>
            </svg>
          </button>
        </div>
      ))}
    </div>
  )
}

export function toast() {}
toast.success = (msg: string) => console.log('success:', msg)
toast.error = (msg: string) => console.log('error:', msg)
toast.warning = (msg: string) => console.log('warning:', msg)
toast.info = (msg: string) => console.log('info:', msg)