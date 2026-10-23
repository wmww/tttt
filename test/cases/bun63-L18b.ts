// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import { x } from "./café";
x;
const sentinel: number = "";
// @filename: café.ts
export const x = 1;
