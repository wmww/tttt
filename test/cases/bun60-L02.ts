// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noImplicitAny": false, "declaration": true, "isolatedDeclarations": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export function f(this, a: number): void {}
export const g = function (this, a: number): void {};
export class A { m(this, v: number): void {} static n(this): void {} }
export interface I { m(this, v: number): void; (this): void; }
export type T = (this, a: number) => void;
export const o = { m(this, a: number): void {} };
export function h(b, a: number): void {}
const sentinel: number = "";
