// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const enum CE { A }
declare const t: [typeof CE, number];
declare function f(a: unknown, b: number): void;
f(...t);
const sentinel: number = "";
