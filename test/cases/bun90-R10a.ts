// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "isolatedModules": true, "esModuleInterop": true } }
// @filename: a.ts
import { G1 as GC, G1 as GN } from "./t";
import c = GC.v;
import d = GN.nope;
c; d;
export const sentinel: number = "";
// @filename: g.d.ts
declare class GC { static v: number }
declare namespace GN { const v: number }
// @filename: t.ts
export interface G1 { x: 1 }
