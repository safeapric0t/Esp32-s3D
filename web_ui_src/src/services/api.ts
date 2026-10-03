import { PrinterStatus, PrintProgress, FileInfo, ApiResponse } from '../types/printer'

const API_BASE = ''
const API_KEY = 'changeme123' // Should be configurable

async function fetchWithAuth(url: string, options: RequestInit = {}): Promise<Response> {
  const headers = new Headers(options.headers)
  headers.set('Authorization', `Bearer ${API_KEY}`)
  headers.set('Content-Type', 'application/json')
  
  return fetch(`${API_BASE}${url}`, {
    ...options,
    headers
  })
}

export const api = {
  // Status
  getStatus: () => fetchWithAuth('/api/status').then(r => r.json()) as Promise<PrinterStatus>,
  getPrinter: () => fetchWithAuth('/api/printer').then(r => r.json()),
  
  // Files
  getFiles: () => fetchWithAuth('/api/files').then(r => r.json()) as Promise<FileInfo[]>,
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
    fetchWithAuth('/api/files/copy-to-printer', {
      method: 'POST',
      body: JSON.stringify({ name })
    }).then(r => r.json()) as Promise<ApiResponse>,
  
  // Print control
  startPrint: (filename: string, mode: 'stream' | 'sd' = 'stream') =>
    fetchWithAuth('/api/print/start', {
      method: 'POST',
      body: JSON.stringify({ file: filename, mode })
    }).then(r => r.json()) as Promise<ApiResponse>,
  pausePrint: () =>
    fetchWithAuth('/api/print/pause', { method: 'POST' }).then(r => r.json()) as Promise<ApiResponse>,
  resumePrint: () =>
    fetchWithAuth('/api/print/resume', { method: 'POST' }).then(r => r.json()) as Promise<ApiResponse>,
  cancelPrint: () =>
    fetchWithAuth('/api/print/cancel', { method: 'POST' }).then(r => r.json()) as Promise<ApiResponse>,
  
  // G-code
  sendGcode: (command: string) =>
    fetchWithAuth('/api/gcode', {
      method: 'POST',
      body: JSON.stringify({ command })
    }).then(r => r.json()) as Promise<ApiResponse>,
  
  // Movement
  move: (axes: { x?: number; y?: number; z?: number; e?: number; feedrate?: number }) =>
    fetchWithAuth('/api/move', {
      method: 'POST',
      body: JSON.stringify(axes)
    }).then(r => r.json()) as Promise<ApiResponse>,
  home: (axes: { x?: boolean; y?: boolean; z?: boolean }) =>
    fetchWithAuth('/api/home', {
      method: 'POST',
      body: JSON.stringify(axes)
    }).then(r => r.json()) as Promise<ApiResponse>,
  
  // Temperature
  setTemperature: (hotend?: number, bed?: number) =>
    fetchWithAuth('/api/temperature', {
      method: 'POST',
      body: JSON.stringify({ hotend, bed })
    }).then(r => r.json()) as Promise<ApiResponse>,
  
  // Fan
  setFan: (fan_id: number, speed: number) =>
    fetchWithAuth('/api/fan', {
      method: 'POST',
      body: JSON.stringify({ fan_id, speed })
    }).then(r => r.json()) as Promise<ApiResponse>
}