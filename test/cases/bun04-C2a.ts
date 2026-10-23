// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type BadReturn = { [Symbol.iterator](): { next(): IteratorResult<number>; return: number } };
function f1<T extends BadReturn>(t: T) { for (const x of t) {} }
const sentinel: number = "";
