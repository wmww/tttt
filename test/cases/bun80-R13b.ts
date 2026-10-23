// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "commonjs", "esModuleInterop": true } }
// @filename: a.ts
import Missin from "./a";
export default Missing;
// @filename: b.ts
export default Missing;
import Missin from "./b";
// @filename: c.ts
import Missin = require("./c");
export = Missing;
// @filename: s.ts
const sentinel: number = "";
