// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "commonjs"}}
// @filename: a.ts
import.defer("./zz",);
const sentinel: number = "";
// @filename: zz.ts
export {};
