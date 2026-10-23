// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Array<T> { 0: string; }
declare function f(...a: number[]): void;
f("s"); f(1); f("s", 1);
declare function g(...cbs: ((x: number) => void)[]): void;
g("s", y => { const t: never = y; });
const fn: (a: number) => void = f;
const sentinel: number = "";
