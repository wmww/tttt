// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export const x = 1;
// @filename: b.ts
export const x = 2;
// @filename: c.ts
export const x = 3;
// @filename: m.ts
export * from "./a";
export * from "./b";
export * from "./c";
import "./main";
const sentinel: number = "";
// @filename: main.ts
export * from "./a";
export * from "./m";
