// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface A11<T = { [K in keyof A11]: 1 }> { x: T }
const sentinel: number = "";
