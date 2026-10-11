import React, { useEffect, useCallback } from 'react'
import { FileInfo, UploadResult } from '../types/printer'
import { api } from '../services/api'

export function useFiles() {
  const [files, setFiles] = React.useState<FileInfo[]>([])
  const [loading, setLoading] = React.useState(true)
  const [error, setError] = React.useState<string | null>(null)
  const [uploading, setUploading] = React.useState(false)
  const [uploadProgress, setUploadProgress] = React.useState(0)

  const fetchFiles = React.useCallback(async () => {
    try {
      const [rawSpiffsFiles, rawSdFiles] = await Promise.all([
        api.getFiles(),
        api.getSdFiles().catch(() => [])
      ])
      const spiffsFiles = Array.isArray(rawSpiffsFiles) ? rawSpiffsFiles : []
      const sdFiles = Array.isArray(rawSdFiles) ? rawSdFiles : []

      // Keep the source returned by the device authoritative.  An ESP32 file
      // and an old SD file may share a name; matching by name alone falsely
      // labels the ESP32 file as SD and makes Start choose the wrong path.
      const espNames = new Set(spiffsFiles.map(file => file.name.toLowerCase()))
      const remainingSd = sdFiles.filter(file => !espNames.has(file.name.toLowerCase()))
      setFiles([...spiffsFiles, ...remainingSd])
      setError(null)
    } catch (e) {
      setError('Failed to fetch files')
      console.error('Files fetch error:', e)
    } finally {
      setLoading(false)
    }
  }, [])

  React.useEffect(() => {
    fetchFiles()
  }, [fetchFiles])

  const upload = async (file: File): Promise<UploadResult | null> => {
    setUploading(true)
    setUploadProgress(0)
    try {
      const response = await api.uploadFile(file)
      if (response?.success === false) throw new Error(response.error || 'Upload failed')
      await fetchFiles()
      return {
        filename: response?.filename || file.name,
        onPrinterSd: Boolean(response?.on_printer_sd),
        warning: response?.warning
      }
    } catch (e) {
      setError('Upload failed')
      return null
    } finally {
      setUploading(false)
      setUploadProgress(0)
    }
  }

  const deleteFile = async (name: string, onPrinterSd = false) => {
    try {
      const response = await api.deleteFile(name, onPrinterSd)
      if (response?.success === false) throw new Error(response.error || 'Delete failed')
      await fetchFiles()
      return true
    } catch (e) {
      setError('Delete failed')
      return false
    }
  }

  const copyToPrinter = async (name: string) => {
    try {
      const response = await api.copyToPrinter(name)
      if (response?.success === false) throw new Error(response.error || 'Copy failed')
      await fetchFiles()
      return true
    } catch (e) {
      setError('Copy to printer failed')
      return false
    }
  }

  return { 
    files, 
    loading, 
    error, 
    uploading, 
    uploadProgress,
    fetchFiles,
    upload,
    deleteFile,
    copyToPrinter
  }
}
