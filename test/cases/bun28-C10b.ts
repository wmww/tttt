// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022"}}
// @filename: a.ts
export function fn<T>() {}
type R = typeof import("./a").fn<>;
const sentinel: number = "";
