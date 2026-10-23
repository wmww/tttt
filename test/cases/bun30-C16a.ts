// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const sentinel: number = "";
// @filename: b.ts
import "./m";
{ export * from "./m"; }
// @filename: c.ts
import { m1 } from "./b";
m1;
// @filename: m.ts
export const m1 = 1;
