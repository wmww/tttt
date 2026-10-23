// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "declaration": true, "isolatedDeclarations": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export class B { set x(this, v: number) {} }
export class H { get x(this): number { return 1; } }
export class I { get x(this): number { return 1; } set x(this, v: number) {} }
export class J { static set x(this, v: number) {} }
export class K { private set x(this, v: number) {} }
export class M { get x(): number { return 1; } set x(this, v: number) {} }
export class N { get x(this) { return 1; } }
export abstract class O { abstract set x(this, v: number); }
export declare class P { set x(this, v: number); }
export interface Q { set x(this, v: number); get y(this): number }
export type R = { set x(this, v: number); get y(this): number }
const sentinel: number = "";
