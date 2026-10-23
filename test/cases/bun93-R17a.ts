// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"]}}
// @filename: a.ts
import * as r from "./r"; export const v = r.w;
// @filename: r.ts
import { v } from "./a"; export { v as w };
// @filename: zz_sentinel.ts
const sentinel: number = "";
export {};
