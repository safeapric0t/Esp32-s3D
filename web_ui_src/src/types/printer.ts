export type PrinterState = 
  | 'idle'
  | 'printing'
  | 'paused'
  | 'error'
  | 'disconnected'
  | 'booting'
  | 'heating'

export interface Temperature {
  current: number
  target: number
  power: number
}

export interface Position {
  x: number
  y: number
  z: number
  e: number
  homed: boolean[]
}

export interface Fan {
  id: number
  speed: number
  enabled: boolean
}

export interface PrintProgress {
  filename: string
  file_size: number
  file_pos: number
  progress: number
  layer_current: number
  layer_total: number
  current_z: number
  time_elapsed: number
  time_remaining: number
  bytes_printed: number
}

export interface PrinterStatus {
  state: PrinterState
  firmware_version: string
  printer_model: string
  hotend: Temperature
  bed: Temperature
  chamber: Temperature
  position: Position
  fans: Fan[]
  fan_count: number
  progress: PrintProgress
  sd_inserted: boolean
  sd_files: string[]
  sd_file_count: number
  usb_connected: boolean
  last_ok_time: number
  consecutive_errors: number
  ws_clients: number
}

export interface FileInfo {
  name: string
  size: number
  date: number
  is_dir: boolean
  on_printer_sd: boolean
}

export interface WSMessage {
  type: 'status' | 'progress' | 'temperature' | 'position' | 'log' | 'file_list' | 'state_change' | 'error'
  data: any
}

export interface ApiResponse {
  success: boolean
  message?: string
  data?: any
}