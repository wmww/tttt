// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type M<T, U> = { [K in keyof T]: M<U, { z: 1 }> };
const x1: keyof M<{ a: 1 }, { b: 2 }>["a"] = "b";
const sentinel: number = "";
