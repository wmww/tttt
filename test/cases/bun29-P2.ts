// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "declaration": true, "noEmit": true, "types": [], "lib": ["es2022"], "paths": { "@re": ["./sub/reexp.ts"] } } }
// @filename: a.ts
import { mk } from "pkg";
export const v = mk();
const sentinel: number = "";
// @filename: node_modules/pkg/index.d.ts
import { Inner } from "inner";
export declare function mk(): Inner;
// @filename: node_modules/pkg/node_modules/inner/index.d.ts
export interface Inner { x: 1 }
// @filename: sub/package.json
{}
// @filename: sub/reexp.ts
import { Inner } from "../node_modules/pkg/node_modules/inner";
export default Inner;
