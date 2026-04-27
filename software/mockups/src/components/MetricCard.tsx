interface MetricCardProps {
  label: string
  value: string | number
  unit?: string
  accent?: string
  icon?: React.ReactNode
}

export function MetricCard({ label, value, unit, accent = 'var(--cyan)', icon }: MetricCardProps) {
  return (
    <div
      className="flex flex-col items-start justify-between"
      style={{
        background: 'var(--surface)',
        border: '1px solid var(--border)',
        borderRadius: 10,
        padding: '10px 14px',
        flex: 1,
      }}
    >
      {/* Label row */}
      <div className="flex items-center gap-1.5" style={{ marginBottom: 4 }}>
        {icon && <span style={{ color: accent, opacity: 0.7 }}>{icon}</span>}
        <span
          className="font-orbitron font-semibold uppercase tracking-widest"
          style={{ fontSize: 9, color: 'var(--muted)', letterSpacing: '0.2em' }}
        >
          {label}
        </span>
      </div>

      {/* Value */}
      <div className="flex items-end gap-0.5">
        <span
          className="font-orbitron font-bold leading-none"
          style={{ fontSize: 28, color: accent }}
        >
          {value}
        </span>
        {unit && (
          <span
            className="font-orbitron font-medium"
            style={{ fontSize: 11, color: accent, opacity: 0.55, paddingBottom: 3 }}
          >
            {unit}
          </span>
        )}
      </div>
    </div>
  )
}
