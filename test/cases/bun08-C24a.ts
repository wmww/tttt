// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
declare module "./r11" { namespace star { interface Added {} } }
import { Zip } from "./n";
const sentinel: number = "";
// @filename: n.ts
export namespace N { export interface X {} }
// @filename: r11.ts
export * as star from "./n";
