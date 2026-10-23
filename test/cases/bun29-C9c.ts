// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let x: { a: typeof Symbol.iterator } & { a: typeof Symbol.asyncIterator };
x.b;
const sentinel: number = "";
