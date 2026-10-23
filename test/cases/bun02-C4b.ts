// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface A4<T = `${A4 & string}`> { x: T }
const sentinel: number = "";
