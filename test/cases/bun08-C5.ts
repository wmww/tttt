// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import {} from "./m";
declare const k: "k";
declare module "./m" {
  interface I { [k](): void }
}
const sentinel: number = "";
// @filename: m.ts
export interface I { k: number }
