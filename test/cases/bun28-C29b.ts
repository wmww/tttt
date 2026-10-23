// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "esnext"}}
// @filename: a.ts
import.foo<string>("./zz");
const sentinel: number = "";
// @filename: zz.ts
export {};
