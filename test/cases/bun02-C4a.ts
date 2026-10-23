// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface A3<T = A3 extends infer U ? U : never> { x: T }
interface A19<T = string extends string ? A19 : never> { x: T }
interface A20<T = string extends A20 ? 1 : never> { x: T }
const sentinel: number = "";
