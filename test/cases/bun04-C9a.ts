// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const badReturn: { [Symbol.iterator](): { next(): IteratorResult<number>; return: number } };
for (const x of badReturn) {}
for (const y of badReturn) {}
const sentinel: number = "";
