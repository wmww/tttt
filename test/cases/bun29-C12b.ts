// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import * as ns from "./m";
let n: number = () => ns.mk();
const sentinel: number = "";
// @filename: m.ts
class C { c = 1 }
export { C as "a-b" };
export declare function mk(): C;
