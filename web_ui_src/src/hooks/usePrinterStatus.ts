import React, { useEffect } from 'react'
import { PrinterStatus, PrintProgress } from '../types/printer'
import { api } from '../services/api'

export function usePrinterStatus(pollInterval = 5000) {
  const [status, setStatus] = React.useState<PrinterStatus | null>(null)
  const [progress, setProgress] = React.useState<PrintProgress | null>(null)
  const [loading, setLoading] = React.useState(true)
  const [error, setError] = React.useState<string | null>(null)

  const fetchStatus = async () => {
    try {
      const data = await api.getStatus()
      setStatus(data)
      setProgress(data.progress)
      setError(null)
    } catch (e) {
      setError('Failed to fetch status')
      console.error('Status fetch error:', e)
    } finally {
      setLoading(false)
    }
  }

  React.useEffect(() => {
    fetchStatus()
    const interval = setInterval(fetchStatus, pollInterval)
    return () => clearInterval(interval)
  }, [pollInterval])

  const refresh = () => {
    setLoading(true)
    fetchStatus()
  }

  return { status, progress, loading, error, refresh }
}