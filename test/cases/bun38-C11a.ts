// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "rootDirs": ["", "sub"] } }
// @filename: b.ts
export const b = 1;
// @filename: sub/s.ts
import { b } from "./b";
export const fromSub: string = b;
export const sentinel: number = "";
