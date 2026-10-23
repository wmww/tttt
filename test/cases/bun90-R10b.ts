// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "isolatedModules": true, "esModuleInterop": true } }
// @filename: a.ts
import c = K.v; c; class K { static v = 1 }
import d = L.v; d; let L = { v: 1 };
import e = En.X.y; e; enum En { X }
import type { G1 as TO } from "./t";
import f = TO.v; f;
function fn(p = g) { } import g = P.v; const P = { v: 1 };
export const sentinel: number = "";
// @filename: t.ts
export interface Reflect { x: 1 }
export interface Intl { x: 1 }
export interface G1 { x: 1 }
export interface G2 { x: 1 }
export interface G3 { x: 1 }
export interface G4 { x: 1 }
export interface G5 { x: 1 }
export interface G6 { x: 1 }
export interface G7 { x: 1 }
export interface G8 { x: 1 }
export type G9 = 1;
export default interface GD { x: 1 }
