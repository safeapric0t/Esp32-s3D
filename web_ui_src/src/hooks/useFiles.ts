import React, { useEffect, useCallback } from 'react'
import { FileInfo } from '../types/printer'
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

      const sdMap = new Map(sdFiles.map(f => [f.name, f]))
      const merged = spiffsFiles.map(f => {
        const sdVersion = sdMap.get(f.name)
        if (sdVersion) {
          sdMap.delete(f.name)
          return { ...f, on_printer_sd: true }
        }
        return f
      })
      const remainingSd = Array.from(sdMap.values())
      setFiles([...merged, ...remainingSd])
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

  const upload = async (file: File): Promise<string | null> => {
    setUploading(true)
    setUploadProgress(0)
    try {
      const response = await api.uploadFile(file)
      if (response?.success === false) throw new Error(response.error || 'Upload failed')
      await fetchFiles()
      return response?.filename || file.name
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
      await api.deleteFile(name, onPrinterSd)
      await fetchFiles()
      return true
    } catch (e) {
      setError('Delete failed')
      return false
    }
  }

  const copyToPrinter = async (name: string) => {
    try {
      await api.copyToPrinter(name)
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
