//// [test/cases/npm_Bitterbot-AI_bitterbot-desktop_TS5088.ts] ////

//// [lib.d.ts]
export type HttpAdapter = (req: {
    on: (event: string) => typeof req;
}) => void;
export declare function cb<A extends HttpAdapter>(a: A): (...args: Parameters<A>) => void;
//// [main.ts]
import { cb, type HttpAdapter } from "./lib";
declare const h: HttpAdapter;
export function f() {
  return { handler: cb(h) };
}
export function g() { return h; }
type P = Parameters<HttpAdapter>;
declare const p: P;
export function k() { return p; }


//// [main.js]
import { cb } from "./lib";
export function f() {
    return { handler: cb(h) };
}
export function g() { return h; }
export function k() { return p; }
