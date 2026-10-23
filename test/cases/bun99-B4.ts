// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2023"], "module": "esnext", "moduleResolution": "bundler", "isolatedModules": true}}
// @filename: a.ts
declare const enum A { X }
import { B } from "./b";
export const v1 = A.X;
export const v2 = A!.X;
export const v3 = A!!.X;
export const v4 = A!!!.X;
export const v5 = (A!!)!.X;
export const v6 = B!!.Y;
export const v7 = [A!!];
export type T = typeof A;
const sentinel: number = "";
// @filename: b.d.ts
export declare const enum B { Y }
