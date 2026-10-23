// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import { nope } from "./é";
const sentinel: number = "";
// @filename: é.ts
export const value = 1;
