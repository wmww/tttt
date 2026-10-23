// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const cond: boolean, x: unknown; type Fn = (n: number) => number;
export const f = cond ? <Fn>(x) : n => n + 1;
export const g = [cond ? <any>(x) : (n: number) => n, cond ? <Fn>(x) : n => n ? 1 : 2];
const sentinel: number = "";
