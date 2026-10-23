// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"]}}
// @filename: a.ts
import x from "/c:/x";
import y from "/^/y";
type T = "/a:/b";
const t: T = "/q:/r";
type U = `/z:/${string}`;
const u: U = "no";
const o = { "/k:/p": 1 };
o["/m:/n"];
export const all = [x, y, t, u];
const sentinel: number = "";
