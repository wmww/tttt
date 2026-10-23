// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import a from "./dir.d.x\\foo.ts";
const sentinel: number = "";
// @filename: dir.d.x/foo.ts
export default 1;
