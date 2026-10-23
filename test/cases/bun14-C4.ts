// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends Record<string, number>>(t: T, s: string) {
  t[s]++;
}
const sentinel: number = "";
