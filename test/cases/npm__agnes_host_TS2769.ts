// origin: npm @agnes/host@0.0.0 https://github.com/AgnesAI-Labs/agnes-harness packages/host/tsconfig.bench.json seed 20261302
// flip: no
// @strict: true
// @target: es2023
const MAC_BROWSER_BUNDLES: ReadonlyMap<string, readonly string[]> = new Map([
  ['com.google.Chrome', ['--new-window']],
  ['com.apple.Safari', []],
] as const)
