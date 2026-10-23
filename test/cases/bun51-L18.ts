// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
export type F1 = ({ a: b }) => void;
export interface I7 { m({ a: b }): void }
export declare class C8 { constructor({ a: b }); }
export class P { private a = 1; m() { return this.a; } }
export const sentinel: number = "";
