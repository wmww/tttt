// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "isolatedModules": true, "experimentalDecorators": true, "emitDecoratorMetadata": true, "module": "esnext" } }
// @filename: a.ts
import { A } from "./m";
declare const dec: any;
class C { @dec m(...a: (A[])) {} }
const sentinel: number = "";
// @filename: m.ts
export interface A {}
