// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import type { T } from "./m";
type U = T<{ a: string }>;
const sentinel: number = "";
// @filename: m.ts
export type { T } from 123;
