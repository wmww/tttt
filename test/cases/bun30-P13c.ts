// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
{ import { m2 } from "./m"; }
const sentinel: number = "";
// @filename: m.ts
export const m2 = 2;
