// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T extends () => unknown>(y: ReturnType<T>) {
  return { ...y };
}
const sentinel: number = "";
