// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function g<T>(u: T, v: typeof y): number;
const y = g(1, 2);
const sentinel: number = "";
