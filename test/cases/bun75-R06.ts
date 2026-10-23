// @filename: tsconfig.json
{"compilerOptions":{"strict":true,"noEmit":true,"types":[],"lib":["es2022"],"module":"esnext","moduleResolution":"bundler"},"files":["p1.ts","p2.ts"]}
// @filename: p1.ts
import * as n1 from "./p2"; export { n1, n1 as m1 }; export default 1;
// @filename: p2.ts
import * as n2 from "./p1"; import * as o2 from "./p1"; export { n2, o2 };
const sentinel: number = "";
