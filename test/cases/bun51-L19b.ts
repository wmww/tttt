// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts", "b.ts"] }
// @filename: a.ts
export type F = ({ a: b }) => void;
export interface I { m({ a: b }): void }
export declare function f({ a: b }): void;
export const sentinel: number = "";
// @filename: b.ts
import { F, I, f } from "./a";
declare const ff: F, i: I;
ff({ a: 1 });
i.m({ a: 1 });
f({ a: 1 });
