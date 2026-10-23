// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function g<T extends unknown[], U extends unknown[]>(): { [K in keyof T]: U } {
  const r: [[1], [1]] = g<[0, ...U], [1]>();
  return null!;
}
const sentinel: number = "";
