// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import type { X } from "./m";
function n1(a = X.y): void; function n1(a: any) {} n1();
const sentinel: number = "";
// @filename: m.ts
export class X { static readonly y = "k"; }
