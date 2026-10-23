// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Brand<T> = { [P in keyof T]: 1 };
type M<T> = { [K in "a" | "b" as K & Brand<T>]: 1 };
function f<T>() { const x: M<T> = {}; }
const sentinel: number = "";
