// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare global { interface PromiseLike<T> { then(cb: (v: "extra") => void): void } }
async function f(p: PromiseLike<number>) { const r: never[] = [await p]; }
const sentinel: number = "";
