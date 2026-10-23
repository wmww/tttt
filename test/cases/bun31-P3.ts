// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I { get x(): this; }
declare const i: I;
const r: null = i.x;
const sentinel: number = "";
