// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] }, "files": ["a.ts"] }
// @filename: a.ts
import "./b";
export declare function f({ a: b }): void;
export const sentinel: number = "";
// @filename: b.ts
import { f } from "./a";
f({ a: 1 });
