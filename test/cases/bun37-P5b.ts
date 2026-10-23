// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "types": [], "lib": ["es2022"], "resolveJsonModule": true }, "files": ["a.ts"] }
// @filename: a.ts
import x from "./x.json";
export { x };
const sentinel: number = "";
// @filename: x.json
{ "a": <div></div> }
