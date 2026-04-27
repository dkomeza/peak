import { useState, useEffect } from 'react'
import { Zap, Wifi, Lightbulb } from 'lucide-react'

interface TopBarProps {
  mountainMode: boolean
  sensorMode: 'PAS' | 'TRQ' | 'HYB'
  batteryPct: number
  lightsOn: boolean
  connected: boolean
  time?: string
}

export function TopBar({ mountainMode, sensorMode, batteryPct, lightsOn, connected, time }: TopBarProps) {
  const [currentTime, setCurrentTime] = useState(time || '')

  useEffect(() => {
    if (time) return
    const update = () => {
      const now = new Date()
      setCurrentTime(now.toLocaleTimeString('en-GB', { hour: '2-digit', minute: '2-digit' }))
    }
    update()
    const id = setInterval(update, 1000)
    return () => clearInterval(id)
  }, [time])

  const battColor = batteryPct > 40 ? '#AAFF00' : batteryPct > 20 ? '#FF8C00' : '#FF3B3B'
  const battFill = `${Math.max(4, batteryPct)}%`

  return (
    <div className="flex items-center justify-between px-4 py-2" style={{ height: 44 }}>
      {/* Mountain Mode Badge */}
      <div className="flex items-center gap-1.5" style={{ minWidth: 110 }}>
        {mountainMode ? (
          <div className="flex items-center gap-1 animate-pulse-amber">
            <Zap size={14} fill="currentColor" style={{ color: 'var(--amber)' }} />
            <span className="font-orbitron text-xs font-bold tracking-widest" style={{ color: 'var(--amber)' }}>
              MOUNTAIN
            </span>
          </div>
        ) : (
          <div className="flex items-center gap-1" style={{ opacity: 0.25 }}>
            <Zap size={14} style={{ color: 'var(--muted)' }} />
            <span className="font-orbitron text-xs tracking-widest" style={{ color: 'var(--muted)' }}>
              MOUNTAIN
            </span>
          </div>
        )}
      </div>

      {/* Sensor Mode Selector */}
      <div
        className="flex items-center rounded-full overflow-hidden"
        style={{ background: 'var(--surface2)', border: '1px solid var(--border)', gap: 1, padding: 2 }}
      >
        {(['PAS', 'TRQ', 'HYB'] as const).map((mode) => (
          <button
            key={mode}
            className="px-2.5 py-0.5 rounded-full font-orbitron text-xs font-semibold tracking-wide transition-all duration-200"
            style={{
              background: sensorMode === mode ? 'var(--cyan)' : 'transparent',
              color: sensorMode === mode ? '#000' : 'var(--muted)',
              fontSize: 10,
            }}
          >
            {mode}
          </button>
        ))}
      </div>

      {/* Status Icons + Battery */}
      <div className="flex items-center gap-2.5" style={{ minWidth: 110, justifyContent: 'flex-end' }}>
        <Lightbulb size={14} style={{ color: lightsOn ? '#FFE066' : 'var(--muted)', opacity: lightsOn ? 1 : 0.4 }} fill={lightsOn ? '#FFE066' : 'none'} />
        <Wifi size={14} style={{ color: connected ? 'var(--cyan)' : 'var(--muted)', opacity: connected ? 1 : 0.4 }} />
        {/* Battery indicator */}
        <div className="flex items-center gap-1">
          <span className="font-orbitron text-xs" style={{ color: battColor, fontSize: 10 }}>
            {batteryPct}%
          </span>
          <div className="relative flex items-center" style={{ width: 24, height: 12 }}>
            <div style={{ width: 22, height: 10, borderRadius: 2, border: `1.5px solid ${battColor}`, position: 'relative', overflow: 'hidden' }}>
              <div style={{ position: 'absolute', left: 0, top: 0, bottom: 0, width: battFill, background: battColor, borderRadius: 1, transition: 'width 0.5s ease' }} />
            </div>
            <div style={{ width: 2, height: 5, background: battColor, borderRadius: '0 1px 1px 0', marginLeft: 1 }} />
          </div>
        </div>
        <span className="font-orbitron text-xs" style={{ color: 'var(--muted)', fontSize: 10 }}>{currentTime}</span>
      </div>
    </div>
  )
}
