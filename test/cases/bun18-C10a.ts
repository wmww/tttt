// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f1(...args: [a: number, ...b: [string]]): void;
const x1: 0 = f1;
const sentinel: number = "";
