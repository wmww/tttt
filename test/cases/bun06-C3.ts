// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class A { get p(): string[] { return [] } set p(v: number[] | string[]) { } }
declare const a: A;
[...a.p] = [1, 2];
const sentinel: number = "";
