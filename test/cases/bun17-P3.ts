// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Array<T> { 0: T }
const t: ArrayLike<string> = ["a", 1];
const sentinel: number = "";
