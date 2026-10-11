import { PrinterStatus, PrintProgress, FileInfo, ApiResponse } from '../types/printer'

const API_BASE = ''
const API_KEY = 'changeme123' // Should be configurable

async function fetchWithAuth(url: string, options: RequestInit = {}): Promise<Response> {
  const headers = new Headers(options.headers)
  headers.set('Authorization', `Bearer ${API_KEY}`)
  if (!(options.body instanceof FormData)) {
    headers.set('Content-Type', 'application/json')
  }
  
  return fetch(`${API_BASE}${url}`, {
    ...options,
    headers
  })
}

export const api = {
  // Status
  getStatus: async () => {
    const payload = await fetchWithAuth('/api/status').then(r => r.json())
    // The ESP32 endpoint wraps status messages as { type, data }.
    return (payload?.data ?? payload) as PrinterStatus
  },
  getPrinter: () => fetchWithAuth('/api/printer').then(r => r.json()),
  
  // Files
  getFiles: () => fetchWithAuth('/api/files').then(r => r.json()) as Promise<FileInfo[]>,
  getSdFiles: () => fetchWithAuth('/api/files/printer-sd').then(r => r.json()) as Promise<FileInfo[]>,
  uploadFile: (file: File) => {
    const formData = new FormData()
    formData.append('file', file)
    return fetchWithAuth('/api/files/upload', {
      method: 'POST',
      headers: {},
      body: formData
    }).then(r => r.json()) as Promise<ApiResponse>
  },
  deleteFile: (name: string, onPrinterSd = false) => 
    fetchWithAuth(`/api/files/delete?name=${encodeURIComponent(name)}&printer_sd=${onPrinterSd}`, {
      method: 'DELETE'
    }).then(r => r.json()) as Promise<ApiResponse>,
  copyToPrinter: (name: string) =>
    fetchWithAuth(`/api/files/copy-to-printer?name=${encodeURIComponent(name)}`, {
      method: 'POST'
    }).then(r => r.json()) as Promise<ApiResponse>,
  
  // Print control
  startPrint: (filename: string, mode: 'stream' | 'sd' = 'stream') =>
    fetchWithAuth(`/api/print/start?file=${encodeURIComponent(filename)}&mode=${mode}`, {
      method: 'POST'
    }).then(r => r.json()) as Promise<ApiResponse>,
  pausePrint: () =>
    fetchWithAuth('/api/print/pause', { method: 'POST' }).then(r => r.json()) as Promise<ApiResponse>,
  resumePrint: () =>
    fetchWithAuth('/api/print/resume', { method: 'POST' }).then(r => r.json()) as Promise<ApiResponse>,
  cancelPrint: () =>
    fetchWithAuth('/api/print/cancel', { method: 'POST' }).then(r => r.json()) as Promise<ApiResponse>,
  
  // G-code
  sendGcode: (command: string) =>
    fetchWithAuth(`/api/gcode?command=${encodeURIComponent(command)}`, {
      method: 'POST'
    }).then(r => r.json()) as Promise<ApiResponse>,
  
  // Movement
  move: (axes: { x?: number; y?: number; z?: number; e?: number; feedrate?: number; relative?: boolean }) => {
    const params = new URLSearchParams(Object.entries(axes).filter(([, value]) => value !== undefined).map(([key, value]) => [key, String(value)]))
    return fetchWithAuth(`/api/move?${params}`, { method: 'POST' }).then(r => r.json()) as Promise<ApiResponse>
  },
  home: (axes: { x?: boolean; y?: boolean; z?: boolean }) => {
    const params = new URLSearchParams(Object.entries(axes).map(([key, value]) => [key, String(value)]))
    return fetchWithAuth(`/api/home?${params}`, { method: 'POST' }).then(r => r.json()) as Promise<ApiResponse>
  },
  
  // Temperature
  setTemperature: (hotend?: number, bed?: number) => {
    const params = new URLSearchParams()
    if (hotend !== undefined) params.set('hotend', String(hotend))
    if (bed !== undefined) params.set('bed', String(bed))
    return fetchWithAuth(`/api/temperature?${params}`, { method: 'POST' }).then(r => r.json()) as Promise<ApiResponse>
  },
  
  // Fan
  setFan: (fan_id: number, speed: number) =>
    fetchWithAuth(`/api/fan?fan_id=${fan_id}&speed=${speed}`, { method: 'POST' }).then(r => r.json()) as Promise<ApiResponse>
}
