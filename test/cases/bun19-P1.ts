// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import * as m4 from "./m4";
const o: typeof m4 = { fn: x => x };
const sentinel: number = "";
// @filename: m1.ts
export const fn = (x: number) => x;
// @filename: m4.ts
export type * from "./m1";
