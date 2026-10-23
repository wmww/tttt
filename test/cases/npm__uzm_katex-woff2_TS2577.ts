// origin: npm @uzm/katex-woff2@0.1.0 https://github.com/KaTeX/KaTeX tsconfig.bench.json seed 20261024
// flip: yes
// @strict: true
// A function type whose return type excludes the function type from the union it is in: tsgo resolves it, tttt
// reports TS2577 (KaTeX's src/strict.ts:10)
export type Strict = boolean | "ignore" | StrictFunction;
export type StrictFunction = (errorCode: string) => Exclude<Strict, StrictFunction>;
