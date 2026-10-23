// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Base = Readonly<{ id: string }>;
declare function g<T>(t: T): T & Base;
declare const b: Base;
g(b).x;
const sentinel: number = "";
