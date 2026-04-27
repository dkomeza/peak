interface GearSelectorProps {
  gear: number // 1–5
  mountainMode: boolean
}

const GEARS = [1, 2, 3, 4, 5]

export function GearSelector({ gear, mountainMode }: GearSelectorProps) {
  const accentColor = mountainMode ? 'var(--amber)' : 'var(--accent)'

  return (
    <div
      className="flex flex-col gap-2 px-4 py-3"
      style={{ background: 'var(--bg)' }}
    >
      {/* Label */}
      <div className="flex items-center justify-between">
        <span className="font-orbitron text-xs tracking-widest uppercase" style={{ fontSize: 9, color: 'var(--muted)' }}>
          ASSIST GEAR
        </span>
        <span className="font-orbitron font-bold" style={{ fontSize: 13, color: accentColor }}>
          G{gear}
        </span>
      </div>

      {/* Gear pips */}
      <div className="flex items-end gap-2">
        {GEARS.map((g) => {
          const isActive = g === gear
          const isFilled = g <= gear
          const height = 10 + g * 8 // stepped height: 18, 26, 34, 42, 50px

          return (
            <div
              key={g}
              className="flex-1 flex flex-col items-center gap-1"
            >
              {/* Bar */}
              <div
                style={{
                  width: '100%',
                  height,
                  borderRadius: 4,
                  background: isFilled
                    ? accentColor
                    : 'var(--surface2)',
                  border: isActive
                    ? `2px solid ${accentColor}`
                    : isFilled
                    ? 'none'
                    : '1px solid var(--border)',
                  boxShadow: isActive
                    ? `0 0 12px ${accentColor}, 0 0 24px ${accentColor}55`
                    : 'none',
                  opacity: isFilled ? 1 : 0.35,
                  transition: 'all 0.3s ease',
                }}
              />
              {/* Label */}
              <span
                className="font-orbitron font-bold"
                style={{
                  fontSize: 10,
                  color: isActive ? accentColor : isFilled ? accentColor : 'var(--muted)',
                  opacity: isActive ? 1 : isFilled ? 0.6 : 0.35,
                  transition: 'color 0.3s ease',
                }}
              >
                {g}
              </span>
            </div>
          )
        })}
      </div>
    </div>
  )
}
