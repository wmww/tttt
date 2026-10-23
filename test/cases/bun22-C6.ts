// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<T>(x: ({ a: 1 } & { b: 1 }) | T): T;
declare const s: ({ a: 1 } & { b: 1 }) | boolean;
const p: boolean = f(s);
const sentinel: number = "";
