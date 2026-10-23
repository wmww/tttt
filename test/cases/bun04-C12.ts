// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "esnext", "module": "esnext", "moduleResolution": "bundler", "verbatimModuleSyntax": true } }
// @filename: a.ts
import { K } from "./m";
export namespace K {}
const sentinel: number = "";
// @filename: m.ts
class K {}
export type { K };
