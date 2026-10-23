// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["index.ts"] }
// @filename: a.ts
import * as sx from "./x";
export { sx };
// @filename: b.ts
import * as sx from "./x";
export { sx };
// @filename: index.ts
export * from "./a";
export * from "./b";
const sentinel: number = "";
// @filename: x.ts
export const X = 1;
export default 1;
