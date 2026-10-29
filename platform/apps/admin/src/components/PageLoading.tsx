function PageLoading({ label = 'Loading…' }: { label?: string }) {
  return (
    <div className="grid gap-8">
      <p role="status" className="text-muted-foreground">
        {label}
      </p>
      <div
        aria-hidden="true"
        className="bg-surface border-border grid animate-pulse rounded-lg border px-6 py-2"
      >
        {[0, 1, 2].map((row) => (
          <div
            key={row}
            className="border-border flex items-center gap-4 py-4 not-last:border-b"
          >
            <div className="bg-surface-raised size-10 rounded-md" />
            <div className="grid flex-1 gap-2">
              <div className="bg-surface-raised h-3.5 w-2/5 rounded-sm" />
              <div className="bg-surface-raised h-3 w-1/4 rounded-sm" />
            </div>
          </div>
        ))}
      </div>
    </div>
  )
}

export { PageLoading }
