// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "types": [], "rootDirs": ["", "gen"], "lib": ["es2022"] }, "files": ["src/a.ts"] }
// @filename: gen/src/b.ts
export const b = 1;
// @filename: src/a.ts
import { b } from "./b";
export const y: number = b;
const sentinel: number = "";
