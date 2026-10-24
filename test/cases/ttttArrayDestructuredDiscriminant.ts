// @strict: true
// A const array pattern element (or a numeric key of an object pattern) of an unannotated variable is a discriminant alias of
// its initializer: `const [t] = c; if (t === "a") c[2]` narrows c (tsc's getCandidateDiscriminantPropertyAccess)
type C = ['a', 1, string] | ['b', 2, number];
declare const o: { c: C };
export function f(c: C, d: [0, 'x', string] | [1, 'y', number]) {
  const [t] = c;
  if (t === 'a') { const s: string = c[2]; }
  const [, k] = d;
  if (k === 'x') { const s: string = d[2]; }
  const [u] = o.c;
  if (u === 'b') { const n: number = o.c[2]; }
  let [v] = c;
  if (v === 'a') { const s: string = c[2]; }
  const [[w]] = [c];
  const [x = 'a'] = c;
  if (x === 'a') { const s: string = c[2]; }
  const { 0: y } = c;
  if (y === 'a') { const s: string = c[2]; }
}
