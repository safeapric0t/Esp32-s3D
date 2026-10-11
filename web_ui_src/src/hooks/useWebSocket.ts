import React, { useEffect, useRef, useCallback } from 'react'
import { WSMessage, PrinterStatus, PrintProgress, FileInfo } from '../types/printer'

interface UseWebSocketOptions {
  onStatus?: (status: PrinterStatus) => void
  onProgress?: (progress: PrintProgress) => void
  onLog?: (level: string, message: string) => void
  onFileList?: (files: FileInfo[]) => void
  onStateChange?: (state: string) => void
  onError?: (error: string) => void
}

export function useWebSocket(options: UseWebSocketOptions = {}) {
  const wsRef = React.useRef<WebSocket | null>(null)
  const optionsRef = React.useRef(options)
  optionsRef.current = options
  const [connected, setConnected] = React.useState(false)
  const reconnectTimeoutRef = React.useRef<NodeJS.Timeout | null>(null)
  const reconnectAttempts = React.useRef(0)
  const maxReconnectAttempts = 10

  const connect = React.useCallback(() => {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
    const wsUrl = `${protocol}//${window.location.host}/ws`
    
    try {
      const ws = new WebSocket(wsUrl)
      wsRef.current = ws

      ws.onopen = () => {
        console.log('WebSocket connected')
        setConnected(true)
        reconnectAttempts.current = 0
      }

      ws.onmessage = (event) => {
        try {
          const msg: WSMessage = JSON.parse(event.data)
          
          switch (msg.type) {
            case 'status':
        optionsRef.current.onStatus?.(msg.data)
              break
            case 'progress':
              optionsRef.current.onProgress?.(msg.data)
              break
            case 'log':
              optionsRef.current.onLog?.(msg.data.level, msg.data.message)
              break
            case 'file_list':
              try {
                const files = typeof msg.data === 'string' ? JSON.parse(msg.data) : msg.data
                optionsRef.current.onFileList?.(Array.isArray(files) ? files : [])
              } catch {
                optionsRef.current.onFileList?.([])
              }
              break
            case 'state_change':
              optionsRef.current.onStateChange?.(msg.data.state_str || msg.data.state)
              break
            case 'error':
              optionsRef.current.onError?.(msg.data)
              break
          }
        } catch (e) {
          console.error('Failed to parse WS message:', e)
        }
      }

      ws.onclose = () => {
        console.log('WebSocket disconnected')
        setConnected(false)
        wsRef.current = null

        // Reconnect with exponential backoff
        if (reconnectAttempts.current < maxReconnectAttempts) {
          const delay = Math.min(1000 * Math.pow(2, reconnectAttempts.current), 30000)
          reconnectAttempts.current++
          reconnectTimeoutRef.current = setTimeout(connect, delay)
        }
      }

      ws.onerror = (error) => {
        console.error('WebSocket error:', error)
      }
    } catch (e) {
      console.error('Failed to create WebSocket:', e)
      setConnected(false)
    }
  }, [])

  const send = React.useCallback((data: object) => {
    if (wsRef.current?.readyState === WebSocket.OPEN) {
      wsRef.current.send(JSON.stringify(data))
    }
  }, [])

  const sendGcode = React.useCallback((command: string) => {
    send({ type: 'gcode', command })
  }, [send])

  React.useEffect(() => {
    connect()
    return () => {
      if (reconnectTimeoutRef.current) clearTimeout(reconnectTimeoutRef.current)
      wsRef.current?.close()
    }
  }, [connect])

  return { connected, send, sendGcode }
}
