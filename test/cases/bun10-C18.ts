// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare class X { static readonly ["a"]: unique symbol; static readonly 1: unique symbol }
const k = { [X.a]: 1, [X[1]]: 3 };
const t: boolean = k;
const sentinel: number = "";
