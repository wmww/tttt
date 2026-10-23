// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import * as ns from "./m";
let n: number = () => ns.mk();
const sentinel: number = "";
// @filename: m.ts
export default class D { d = 1 }
export declare function mk(): D;
