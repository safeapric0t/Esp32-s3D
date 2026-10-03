import { useState, useEffect, useCallback } from 'react'
import { FileInfo } from '../types/printer'
import { api } from '../services/api'

export function useFiles() {
  const [files, setFiles] = useState<FileInfo[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)
  const [uploading, setUploading] = useState(false)
  const [uploadProgress, setUploadProgress] = useState(0)

  const fetchFiles = useCallback(async () => {
    try {
      const data = await api.getFiles()
      setFiles(data)
      setError(null)
    } catch (e) {
      setError('Failed to fetch files')
      console.error('Files fetch error:', e)
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => {
    fetchFiles()
  }, [fetchFiles])

  const upload = async (file: File) => {
    setUploading(true)
    setUploadProgress(0)
    try {
      await api.uploadFile(file)
      await fetchFiles()
      return true
    } catch (e) {
      setError('Upload failed')
      return false
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