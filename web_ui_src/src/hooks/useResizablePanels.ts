import { useState, useCallback, useRef, useEffect } from 'react'

interface UseResizablePanelsOptions {
  direction: 'horizontal' | 'vertical'
  minSize?: number
  maxSize?: number
  defaultSizes?: number[]
  onChange?: (sizes: number[]) => void
}

export function useResizablePanels({
  direction,
  minSize = 200,
  maxSize = Infinity,
  defaultSizes = [],
  onChange
}: UseResizablePanelsOptions) {
  const [sizes, setSizes] = useState<number[]>(() => {
    if (defaultSizes.length > 0) return defaultSizes
    return direction === 'horizontal' ? [50, 50] : [50, 50]
  })
  const [draggingIndex, setDraggingIndex] = useState<number | null>(null)
  const startPosRef = useRef<number>(0)
  const startSizesRef = useRef<number[]>([])

  const handleMouseDown = useCallback((index: number, e: React.MouseEvent) => {
    e.preventDefault()
    e.stopPropagation()
    setDraggingIndex(index)
    startPosRef.current = direction === 'horizontal' ? e.clientX : e.clientY
    startSizesRef.current = [...sizes]
    
    document.body.style.cursor = direction === 'horizontal' ? 'col-resize' : 'row-resize'
    document.body.style.userSelect = 'none'
  }, [direction, sizes])

  const handleMouseMove = useCallback((e: MouseEvent) => {
    if (draggingIndex === null) return
    
    const currentPos = direction === 'horizontal' ? e.clientX : e.clientY
    const delta = currentPos - startPosRef.current
    
    const containerSize = direction === 'horizontal' 
      ? window.innerWidth 
      : window.innerHeight
    
    const percentDelta = (delta / containerSize) * 100
    
    const newSizes = [...startSizesRef.current]
    newSizes[draggingIndex] = Math.max(minSize / containerSize * 100, 
      Math.min(maxSize / containerSize * 100, startSizesRef.current[draggingIndex] + percentDelta))
    newSizes[draggingIndex + 1] = 100 - newSizes[draggingIndex]
    
    setSizes(newSizes)
    onChange?.(newSizes)
  }, [draggingIndex, direction, minSize, maxSize, onChange])

  const handleMouseUp = useCallback(() => {
    setDraggingIndex(null)
    document.body.style.cursor = ''
    document.body.style.userSelect = ''
  }, [])

  useEffect(() => {
    document.addEventListener('mousemove', handleMouseMove)
    document.addEventListener('mouseup', handleMouseUp)
    return () => {
      document.removeEventListener('mousemove', handleMouseMove)
      document.removeEventListener('mouseup', handleMouseUp)
    }
  }, [handleMouseMove, handleMouseUp])

  return {
    sizes,
    setSizes,
    draggingIndex,
    handleMouseDown
  }
}

export function usePanelLayout() {
  const [panelStates, setPanelStates] = useState<Record<string, {
    collapsed: boolean
    minimized: boolean
    size?: { width: number; height: number }
  }>>({})

  const toggleCollapse = useCallback((panelId: string) => {
    setPanelStates(prev => ({
      ...prev,
      [panelId]: { ...prev[panelId], collapsed: !prev[panelId]?.collapsed }
    }))
  }, [])

  const toggleMinimize = useCallback((panelId: string) => {
    setPanelStates(prev => ({
      ...prev,
      [panelId]: { ...prev[panelId], minimized: !prev[panelId]?.minimized }
    }))
  }, [])

  const setPanelSize = useCallback((panelId: string, size: { width: number; height: number }) => {
    setPanelStates(prev => ({
      ...prev,
      [panelId]: { ...prev[panelId], size }
    }))
  }, [])

  return {
    panelStates,
    toggleCollapse,
    toggleMinimize,
    setPanelSize
  }
}