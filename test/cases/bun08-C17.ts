// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import {} from "./r4";
declare module "./r4" { var w: number; }
const sentinel: number = "";
// @filename: n.ts
export var w = 1;
// @filename: r4.ts
export { w } from "./n";
