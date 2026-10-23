// origin: npm @skastr0/quartz-engine@0.2.3 https://github.com/skastr0/quartz tsconfig.bench.json seed 20261025
// flip: yes
// A generic arrow's conditional return annotation and an `as` of the same conditional written again: tsgo reports TS2719
// (the two V's differ: the false branch's mapped property 'V | undefined' isn't related), tttt finds them assignable
// @strict: true
const optionalOption = <K extends string, V>(
  key: K,
  value: V | undefined,
): V extends undefined ? Record<never, never> : { readonly [P in K]?: V } =>
  (value === undefined ? {} : { [key]: value }) as V extends undefined ? Record<never, never> : { readonly [P in K]?: V }
