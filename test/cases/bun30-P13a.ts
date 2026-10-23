// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
{ import { m2 } from "./m"; m2; }
const sentinel: number = "";
// @filename: m.ts
export const m2 = 2;
