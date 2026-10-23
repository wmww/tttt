// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "allowJs": true, "checkJs": true } }
// @filename: a.ts
import { a2 } from "./b";
export const other = 1;
const sentinel: number = "";
// @filename: b.ts
import { a2 } from "./a";
export const other = 1;
// @filename: c.ts
import { s2 } from "./c";
export { s2 as other };
