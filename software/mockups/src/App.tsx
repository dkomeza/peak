import { useState, useEffect } from 'react'
import './index.css'
import { TopBar } from './components/TopBar'
import { SpeedDisplay } from './components/SpeedDisplay'
import { MetricCard } from './components/MetricCard'
import { BatteryBar } from './components/BatteryBar'
import { GearSelector } from './components/GearSelector'
import { Activity, Zap, Timer, Mountain, Route } from 'lucide-react'

// ── Demo state (simulated live data) ──────────────────────────────────────────
function useSimulatedData() {
  const [speed, setSpeed] = useState(24.3)
  const [power, setPower] = useState(312)
  const [cadence, setCadence] = useState(78)
  const [distance, setDistance] = useState(18.4)
  const [elapsed, setElapsed] = useState(3725) // seconds
  const [elevation, setElevation] = useState(248)

  useEffect(() => {
    const id = setInterval(() => {
      setSpeed(s => Math.max(0, +(s + (Math.random() - 0.45) * 0.3).toFixed(1)))
      setPower(p => Math.max(0, Math.round(p + (Math.random() - 0.5) * 15)))
      setCadence(c => Math.max(0, Math.round(c + (Math.random() - 0.5) * 3)))
      setDistance(d => +(d + 0.001).toFixed(3))
      setElapsed(e => e + 1)
      setElevation(e => Math.max(0, Math.round(e + (Math.random() - 0.45) * 1)))
    }, 1000)
    return () => clearInterval(id)
  }, [])

  const formatTime = (s: number) => {
    const h = Math.floor(s / 3600)
    const m = Math.floor((s % 3600) / 60)
    const sec = s % 60
    return `${h}:${String(m).padStart(2, '0')}:${String(sec).padStart(2, '0')}`
  }

  return { speed, power, cadence, distance, elapsed, elevation, formatTime }
}

// ── App ────────────────────────────────────────────────────────────────────────
function App() {
  const { speed, power, cadence, distance, elapsed, elevation, formatTime } = useSimulatedData()

  // Controllable state (buttons)
  const [mountainMode, setMountainMode] = useState(false)
  const [sensorMode, setSensorMode] = useState<'PAS' | 'TRQ' | 'HYB'>('PAS')
  const [gear, setGear] = useState(3)
  const [lightsOn, setLightsOn] = useState(false)

  // Static rider data
  const batteryPct = 72
  const rangeKm = 34

  return (
    <div className="flex flex-col items-center justify-center" style={{ minHeight: '100vh', padding: 16, background: '#060708' }}>
      <p style={{ color: '#333', fontSize: 10, fontFamily: 'Inter, sans-serif', marginBottom: 8, letterSpacing: '0.15em' }}>
        PEAK E-BIKE DISPLAY • 480×640 • 2.8"
      </p>

      {/* ── Display frame ── */}
      <div
        className={`display-root ${mountainMode ? 'mountain-mode-active' : ''}`}
        style={{ transition: 'box-shadow 0.5s ease' }}
      >
        {/* Subtle scan line overlay */}
        <div
          style={{
            position: 'absolute', inset: 0, pointerEvents: 'none', zIndex: 10,
            backgroundImage: 'repeating-linear-gradient(0deg, transparent, transparent 2px, rgba(0,0,0,0.04) 2px, rgba(0,0,0,0.04) 4px)',
          }}
        />

        {/* ── TOP BAR ── */}
        <TopBar
          mountainMode={mountainMode}
          sensorMode={sensorMode}
          batteryPct={batteryPct}
          lightsOn={lightsOn}
          connected={true}
        />

        {/* ── Divider ── */}
        <div style={{ height: 1, background: 'var(--border)', margin: '0 16px' }} />

        {/* ── SPEED ── */}
        <SpeedDisplay speed={speed} mountainMode={mountainMode} />

        {/* ── Divider ── */}
        <div style={{ height: 1, background: 'var(--border)', margin: '0 16px' }} />

        {/* ── METRICS GRID ── */}
        <div className="flex flex-col gap-2 px-4 py-3" style={{ flex: 1 }}>
          <div className="flex gap-2">
            <MetricCard
              label="Motor"
              value={power}
              unit="W"
              accent="#FF8C00"
              icon={<Zap size={10} />}
            />
            <MetricCard
              label="Cadence"
              value={cadence}
              unit="rpm"
              accent="var(--cyan)"
              icon={<Activity size={10} />}
            />
          </div>
          <div className="flex gap-2">
            <MetricCard
              label="Distance"
              value={distance.toFixed(1)}
              unit="km"
              accent="#A78BFA"
              icon={<Route size={10} />}
            />
            <MetricCard
              label="Elapsed"
              value={formatTime(elapsed)}
              accent="#F472B6"
              icon={<Timer size={10} />}
            />
          </div>
          <div className="flex gap-2">
            <MetricCard
              label="Elevation"
              value={elevation}
              unit="m"
              accent="#67E8F9"
              icon={<Mountain size={10} />}
            />
            <MetricCard
              label="Avg Speed"
              value={((distance / elapsed) * 3600).toFixed(1)}
              unit="km/h"
              accent="var(--accent)"
            />
          </div>
        </div>

        {/* ── BATTERY BAR ── */}
        <BatteryBar pct={batteryPct} rangeKm={rangeKm} />

        {/* ── GEAR SELECTOR ── */}
        <GearSelector gear={gear} mountainMode={mountainMode} />
      </div>

      {/* ── Demo Controls (outside display) ── */}
      <div
        className="flex flex-wrap gap-2 justify-center"
        style={{ marginTop: 16, maxWidth: 480 }}
      >
        <button
          onClick={() => setMountainMode(m => !m)}
          style={{
            background: mountainMode ? '#FF8C00' : 'var(--surface2)',
            color: mountainMode ? '#000' : '#AAFF00',
            border: `1px solid ${mountainMode ? '#FF8C00' : '#333'}`,
            borderRadius: 6, padding: '6px 14px',
            fontFamily: 'Orbitron, sans-serif', fontSize: 10,
            fontWeight: 700, letterSpacing: '0.15em', cursor: 'pointer',
            transition: 'all 0.3s ease',
          }}
        >
          ⚡ {mountainMode ? 'MOUNTAIN ON' : 'MOUNTAIN OFF'}
        </button>
        {(['PAS', 'TRQ', 'HYB'] as const).map(m => (
          <button key={m}
            onClick={() => setSensorMode(m)}
            style={{
              background: sensorMode === m ? 'var(--cyan)' : '#141618',
              color: sensorMode === m ? '#000' : '#666',
              border: '1px solid #333',
              borderRadius: 6, padding: '6px 14px',
              fontFamily: 'Orbitron, sans-serif', fontSize: 10, fontWeight: 700,
              letterSpacing: '0.1em', cursor: 'pointer', transition: 'all 0.2s ease',
            }}
          >{m}</button>
        ))}
        <button onClick={() => setGear(g => Math.max(1, g - 1))}
          style={{ background: '#141618', color: '#666', border: '1px solid #333', borderRadius: 6, padding: '6px 12px', fontFamily: 'Orbitron, sans-serif', fontSize: 11, cursor: 'pointer' }}
        >◀ G</button>
        <button onClick={() => setGear(g => Math.min(5, g + 1))}
          style={{ background: '#141618', color: '#666', border: '1px solid #333', borderRadius: 6, padding: '6px 12px', fontFamily: 'Orbitron, sans-serif', fontSize: 11, cursor: 'pointer' }}
        >G ▶</button>
        <button onClick={() => setLightsOn(l => !l)}
          style={{
            background: lightsOn ? '#FFE066' : '#141618',
            color: lightsOn ? '#000' : '#666',
            border: '1px solid #333', borderRadius: 6,
            padding: '6px 14px', fontFamily: 'Orbitron, sans-serif',
            fontSize: 10, fontWeight: 700, cursor: 'pointer', transition: 'all 0.2s ease',
          }}
        >💡 LIGHTS</button>
      </div>
    </div>
  )
}

export default App
