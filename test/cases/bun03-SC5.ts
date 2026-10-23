// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import "./m";
declare module "./m" {
  import z = require("./m");
}
const sentinel: number = "";
// @filename: m.ts
export const a = 1;
