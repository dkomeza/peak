interface BatteryBarProps {
  pct: number
  rangeKm: number
}

const SEGMENTS = 20

export function BatteryBar({ pct, rangeKm }: BatteryBarProps) {
  const filledSegments = Math.round((pct / 100) * SEGMENTS)
  const battColor = pct > 40 ? '#AAFF00' : pct > 20 ? '#FF8C00' : '#FF3B3B'
  const label = pct > 40 ? 'BATTERY' : pct > 20 ? 'LOW BATTERY' : 'CRITICAL'

  return (
    <div
      className="flex flex-col gap-2 px-4 py-3"
      style={{ background: 'var(--surface)', borderTop: '1px solid var(--border)', borderBottom: '1px solid var(--border)' }}
    >
      {/* Header row */}
      <div className="flex items-center justify-between">
        <span className="font-orbitron text-xs font-semibold tracking-widest uppercase" style={{ fontSize: 9, color: 'var(--muted)' }}>
          {label}
        </span>
        <div className="flex items-center gap-3">
          <span className="font-orbitron font-bold" style={{ fontSize: 13, color: battColor }}>
            {pct}%
          </span>
          <span className="font-orbitron" style={{ fontSize: 11, color: 'var(--muted)' }}>
            ~{rangeKm} km
          </span>
        </div>
      </div>

      {/* Segmented bar */}
      <div className="flex gap-1">
        {Array.from({ length: SEGMENTS }).map((_, i) => (
          <div
            key={i}
            style={{
              flex: 1,
              height: 10,
              borderRadius: 2,
              background: i < filledSegments ? battColor : 'var(--surface2)',
              boxShadow: i < filledSegments && i === filledSegments - 1 ? `0 0 6px ${battColor}` : 'none',
              opacity: i < filledSegments ? 1 : 0.4,
              transition: 'background 0.3s ease',
            }}
          />
        ))}
      </div>
    </div>
  )
}
