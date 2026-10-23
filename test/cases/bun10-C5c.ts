// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import d from "./c";
export const yd = d;
const sentinel: number = "";
// @filename: c.ts
import { yd } from "./a";
export default yd;
