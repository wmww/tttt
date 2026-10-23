// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Base<T> { base: T }
interface A extends Base<typeof arr> { x: number }
interface C { x: number }
declare let c: C; declare let a: A;
const arr = [a, c];
const arr2 = [a, c];
const n2: number = arr2;
const sentinel: number = "";
