// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const cond: boolean;
declare function g3<T extends unknown[]>(x: number, ...args: [...T, number]): T;
const r3: never = g3(0, ...(cond ? ["a", 1] : [true, 2]));
const sentinel: number = "";
