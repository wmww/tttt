// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import { X } from "./m";
type X = string;
let n: number = () => mkX();
const sentinel: number = "";
// @filename: g.d.ts
interface X { gx: number }
declare function mkX(): X;
// @filename: m.ts
export const X = 1;
