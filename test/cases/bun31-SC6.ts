// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noImplicitAny": false, "noUnusedLocals": true, "noUnusedParameters": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import { Foo } from "./b"; import { Bar } from "./b"; import { Baz } from "./b";
export type F1 = (Foo) => void;
export interface F2 { m(Bar): void; (Baz): void }
export type F4 = <T>(T) => void;
export type F9 = (Local) => void; type Local = 1;
export const sentinel: number = "";
// @filename: b.ts
export interface Foo { } export interface Bar { } export interface Baz { }
