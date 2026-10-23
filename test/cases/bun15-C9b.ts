// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const cond: boolean;
declare function f1d<const A extends readonly unknown[]>(...args: A): A;
const r1d: never = f1d(...(cond ? [1] : ["a", true]));
const sentinel: number = "";
