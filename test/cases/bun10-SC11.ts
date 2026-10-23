// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: g.ts
interface Array<T> extends Array<T> {}
interface ReadonlyArray<T> extends ReadonlyArray<T> {}
const sentinel: number = "";
