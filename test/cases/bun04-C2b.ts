// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type A = { [Symbol.iterator](): Iterator<number>; [Symbol.iterator](x?: string): Iterator<boolean> };
type B = { [Symbol.iterator](): Iterator<string>; [Symbol.iterator](x?: number): Iterator<bigint> };
function f9<T extends A | B>(t: T) { for (const x of t) { const n: never = x; } }
const sentinel: number = "";
