// origin: npm @framelia/dashboard-server@0.0.5 https://github.com/hungify/framelia packages/dashboard-server/tsconfig.bench.json seed 20261111
// flip: yes
// @strict: true
// @noUncheckedIndexedAccess: true
type V = "queued" | "running" | "masked-pass";
type S = Record<Exclude<V, "masked-pass">, number> & { "masked-pass"?: number; total: number };
declare const s: S;
declare const c: { status: V };
s[c.status] += 1;
