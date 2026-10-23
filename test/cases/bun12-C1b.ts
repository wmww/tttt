// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends any[], U extends any[]>(t: T, u: U): { [K in keyof T]: [T[K], U] } {
  const r = f([...u] as [...U], [1] as [number]);
  const bad: string = r;
  return null!;
}
const sentinel: number = "";
