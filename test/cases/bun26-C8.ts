// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T>(x: T): T;
const r9: string = f([[[[[[[[[(z) => 1]]]]]]]]]);
const sentinel: number = "";
