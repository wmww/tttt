// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function async<T>(x: T): T;
async <number,>(1);
const sentinel: number = "";
