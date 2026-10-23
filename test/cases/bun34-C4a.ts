// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import type { u } from "./m";
type F = ({ [u]: [] }: any) => void;
const sentinel: number = "";
// @filename: m.ts
export const u = "k";
