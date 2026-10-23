// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const bad: { [Symbol.iterator](): { next(): { done: boolean; value: number }; return: number } };
declare function f(...a: number[]): void;
f(...bad);
const arr = [...bad];
const sentinel: number = "";
