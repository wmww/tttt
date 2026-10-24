// @strict: true
// A lone spread of a union whose generic members are constrained to mutable arrays or tuples spreads that union,
// those members as they are (tsc's getMutableArrayOrTupleType): r(...v) infers `X | Z`, not `any[]`
declare function r<A extends any[]>(...a: A): A;
declare function s(...a: string[]): void;
declare function n(a: number, b: string): void;
export function f<X extends string[], Y extends readonly string[], Z extends [number, string]>(x: X, y: Y, z: Z, u: X | Y, v: X | Z, w: X | [string], t: Z | [number, string]) {
  const a1 = r(...u);
  const a2 = r(...v);
  const a3 = r(...w);
  s(...u); s(...v); s(...w);
  n(...t); n(...v); n(...z);
  const a4: [] = r(...v);
  const a5: [] = r(...w);
  const a6: [] = r(...t);
}
