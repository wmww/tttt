// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "target": "esnext", "module": "esnext", "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare global {
  interface Array<T> { [Symbol.asyncIterator](): AsyncIterator<"async">; }
  interface String { [Symbol.iterator](): Iterator<"aug">; }
}
declare const a: number[];
for await (const x of a) { const n: never = x; }
for (const x of "abc") { const n: never = x; }
const sentinel: number = "";
