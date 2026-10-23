// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "dom"], "module": "esnext", "moduleResolution": "bundler", "noUnusedLocals": true } }
// @filename: a.ts
import { Event, Response, Request, URL } from "./t";
import type { Headers } from "./t";
export const e = new Event("x");
export const r = new Response();
export const h = new Headers();
export const u = new URL("http://x");
export function f(q: Request) { return new Request("http://x"); }
export const sentinel: number = "";
// @filename: t.ts
export interface Event { mine: 1 }
export type Response = { r: 1 };
export interface Request { q: 1 }
export interface Headers { h: 1 }
export namespace URL { export type Part = string }
