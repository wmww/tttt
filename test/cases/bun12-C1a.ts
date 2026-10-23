// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function pick<T, U>(t: T, u: U): { [K in keyof T]: U } {
  const r = pick(u, t);
  const ok: { [K in keyof U]: T } = r;
  return null!;
}
const sentinel: number = "";
