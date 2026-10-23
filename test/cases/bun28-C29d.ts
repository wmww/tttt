// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "commonjs"}}
// @filename: a.ts
import<string>("./zz",);
const sentinel: number = "";
// @filename: zz.ts
export {};
