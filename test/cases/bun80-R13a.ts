// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "commonjs", "esModuleInterop": true }, "files": ["a.ts", "b.ts", "s.ts"] }
// @filename: 0.ts
import d from "./a";
d;
// @filename: a.ts
import "./0";
import y from "./a";
export default Missing;
// @filename: b.ts
import other from "./r";
export default Missing;
// @filename: r.ts
export { default } from "./b";
// @filename: s.ts
const sentinel: number = "";
