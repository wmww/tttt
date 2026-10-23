// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["m.ts", "aug1.ts", "aug2.ts", "a.ts"] }
// @filename: a.ts
import { N } from "./m";
let x1: N.Inner.Nope;
let x2: N.Nope;
let x3: N.Only1.Nope;
let x4: N.Inner.Deep.Nope;
const sentinel: number = "";
// @filename: aug1.ts
export {};
declare module "./m" { namespace N { namespace Inner { interface A {} namespace Deep { interface A {} } } namespace Only1 { interface A {} } } }
// @filename: aug2.ts
export {};
declare module "./m" { namespace N { namespace Inner { interface B {} namespace Deep { interface B {} } } } }
// @filename: m.ts
export namespace N { export const a = 1 }
