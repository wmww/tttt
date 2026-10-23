// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "allowJs": true, "checkJs": true } }
// @filename: a.ts
import d from "./m";
export const x = d.foo;
const sentinel: number = "";
// @filename: m.ts
import { x } from "./a";
const o = { foo: x };
export = o;
