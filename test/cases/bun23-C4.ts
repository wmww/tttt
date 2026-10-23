// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export namespace Q { namespace H2 { export enum P { A, B } } export const v = H2.P.A as H2.P; }
export const l1 = Q.v;
const sentinel: number = "";
