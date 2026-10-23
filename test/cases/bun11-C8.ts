// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function id<T>(x: T): T;
const g1 = id(function* () { yield 1; });
const t1: string = g1;
const sentinel: number = "";
