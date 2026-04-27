interface SpeedDisplayProps {
  speed: number
  unit?: string
  mountainMode: boolean
}

export function SpeedDisplay({ speed, unit = 'km/h', mountainMode }: SpeedDisplayProps) {
  const intPart = Math.floor(speed).toString().padStart(2, ' ')
  const decPart = (speed % 1).toFixed(1).slice(1) // '.X'

  return (
    <div className="flex flex-col items-center justify-center" style={{ flex: '0 0 auto', paddingTop: 4, paddingBottom: 4 }}>
      {/* Speed number */}
      <div className="flex items-end gap-1">
        <span
          className="font-orbitron font-black leading-none"
          style={{
            fontSize: 128,
            lineHeight: 1,
            color: mountainMode ? 'var(--amber)' : 'var(--accent)',
            textShadow: mountainMode
              ? '0 0 30px rgba(255,140,0,0.6), 0 0 60px rgba(255,140,0,0.3)'
              : '0 0 30px rgba(170,255,0,0.5), 0 0 60px rgba(170,255,0,0.2)',
            transition: 'color 0.5s ease, text-shadow 0.5s ease',
            letterSpacing: '-4px',
          }}
        >
          {intPart}
        </span>
        <span
          className="font-orbitron font-bold leading-none"
          style={{
            fontSize: 52,
            paddingBottom: 12,
            color: mountainMode ? 'rgba(255,140,0,0.6)' : 'rgba(170,255,0,0.6)',
            letterSpacing: '-2px',
            transition: 'color 0.5s ease',
          }}
        >
          {decPart}
        </span>
      </div>

      {/* Unit */}
      <span
        className="font-orbitron font-medium tracking-widest uppercase"
        style={{
          fontSize: 13,
          color: mountainMode ? 'rgba(255,140,0,0.5)' : 'rgba(170,255,0,0.5)',
          letterSpacing: '0.3em',
          marginTop: -4,
          transition: 'color 0.5s ease',
        }}
      >
        {unit}
      </span>
    </div>
  )
}
