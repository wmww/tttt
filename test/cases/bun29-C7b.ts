// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "declaration": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import { mk } from "./m";
export const v = mk();
const sentinel: number = "";
// @filename: m.ts
namespace Hid { export interface I { i: 1 } }
export declare function mk(): Hid.I;
