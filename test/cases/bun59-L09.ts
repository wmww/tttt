// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "node16" } }
// @filename: a.b/c.ts
export const c = 1;
// @filename: b.mts
import {} from ".\\x";
import {} from "./a.b\\c";
export {};
// @filename: sub/a.mts
import {} from "..\\x";
import {} from "../a.b\\c";
import {} from "..\\nothing";
import {} from "../x\\";
import {} from "../x";
import {} from "..";
import {} from ".";
import {} from "../x/";
const sentinel: number = "";
// @filename: x.ts
export const x = 1;
