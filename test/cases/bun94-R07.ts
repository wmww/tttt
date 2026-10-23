// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare global {
  interface String { me(): this; [Symbol.iterator](): IterableIterator<this>; }
  interface Number { me(): this }
}
function f<D extends string, N extends number>(d: D, n: N) {
  const a: D = d.me();
  const b: string = d.me();
  const c: N = n.me();
  const e: number = n.me();
  for (const z of d) { const g: D = z; }
  const [h]: D[] = [...d];
}
const sentinel: number = "";
