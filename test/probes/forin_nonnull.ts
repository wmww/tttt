// @strict: true
declare const params: { benchmarks?: Record<string, () => void> };
for (const key in params.benchmarks) {
  params.benchmarks[key]();
}
declare const b: Record<string, () => void> | undefined;
for (const key in b) {
  b[key]();
}
