// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import * as ns from "./empty";
function n(x: never) {}
const mm = ns;
if (!mm) n(mm);
const sentinel: number = "";
// @filename: empty.ts
export {}; const q = 1;
