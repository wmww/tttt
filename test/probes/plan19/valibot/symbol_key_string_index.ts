// @strict: true
// A symbol key reads a string index signature when nothing else applies (tsc's getPropertyTypeForIndexType), with
// TS2538 where it is written
interface E { [k: string]: { t?: { issue: string } } }
type X = E[symbol];
const x: { t?: { issue: string } } = null! as X;
function f<T extends { e: E }, K extends readonly (keyof T['e'])[]>(k: K, v: E[K[number] & string]) {
  const y: { t?: { issue: string } } = v;
}
