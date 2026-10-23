// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export default class<T> { a!: T }
export default interface I<U> { b: U }
const sentinel: number = "";
