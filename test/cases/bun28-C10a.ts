// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022"}}
// @filename: a.ts
export interface G<T> { a: T }
type Q = import("./a").G<1,>;
const sentinel: number = "";
