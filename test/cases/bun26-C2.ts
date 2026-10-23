// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f<T>({ a = null! as T }) {}
f<string>({ a: "x" });
const sentinel: number = "";
