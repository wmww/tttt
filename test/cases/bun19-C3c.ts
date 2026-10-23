// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import * as m4 from "./m4";
declare function pick<T>(x: { a?: T }): T;
const t: string = pick(m4);
const sentinel: number = "";
// @filename: m1.ts
export const a = 1;
// @filename: m4.ts
export type * from "./m1";
