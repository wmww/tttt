// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Brand<T> = { [P in keyof T]: 1 };
type M2<T> = { [K in ("a" & Brand<T>) | "b"]: 1 };
function g<T>() { const x: M2<T> = { b: 1 }; }
const sentinel: number = "";
