// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let v1: B32;
interface B32<T = B33> { x: T }
interface B33<T = B32> { x: T }
const sentinel: number = "";
