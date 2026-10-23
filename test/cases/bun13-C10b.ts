// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f(a: string): void;
declare function g(a: string): void;
declare let x2: Parameters<typeof f> | Parameters<typeof g>;
const n2: never = x2;
const sentinel: number = "";
