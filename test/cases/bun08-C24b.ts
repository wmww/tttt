// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
declare module "./r11" { namespace star { interface Added {} } }
declare module "./r8" { namespace N { interface Y {} } }
import { N } from "./n";
let z1: N.Nope;
const sentinel: number = "";
// @filename: n.ts
export namespace N { export interface X {} }
// @filename: r11.ts
export * as star from "./n";
// @filename: r8.ts
export { N } from "./n";
