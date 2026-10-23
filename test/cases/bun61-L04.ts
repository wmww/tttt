// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "declaration": true, "isolatedDeclarations": true } }
// @filename: a.ts
export const o1 = { m(this, a: number): void {} };
export const f1 = function (this, a: number): void {};
export function g1(this, a: number): void {}
export class C { m(this, a: number): void {} }
export interface I { m(this, a: number): void }
export const f2 = function (this: string, a: number): void {};
const sentinel: number = "";
