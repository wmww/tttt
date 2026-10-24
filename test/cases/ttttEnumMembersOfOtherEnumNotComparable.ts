// @strict: true
// Members of two different enums are not comparable even with the same value (tsc's isEnumTypeRelatedTo):
// `t !== D.B` keeps a `T.B`, `t === D.B` has no overlap; a plain literal of the same value is comparable either way
enum T { B = 'boolean' }
enum D { B = 'boolean' }
export function f(x: D | T, t: T) {
  if (t !== D.B) { const a = t; }
  if (x !== D.B) { const b = x; }
  if (x === D.B) { const c = x; }
  if (t === 'boolean') { const d = t; }
  if ('boolean' === t) { const e = t; }
}
const w = D.B as T;
const s = 'boolean' as T;
