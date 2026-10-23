// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Bad2 = { [Symbol.iterator](): { next(): { done: boolean } } };
declare const bad2: Bad2;
const q: Bad2 | number[] = [1];
const sp2 = [...bad2];
const sentinel: number = "";
