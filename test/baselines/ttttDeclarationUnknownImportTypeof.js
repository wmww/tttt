//// [test/cases/ttttDeclarationUnknownImportTypeof.ts] ////

//// [a.ts]
import type { Opts } from "missing-core";
import type { CODES } from "missing-core/error";
export function f<O extends Opts>() { return {} as { e: Partial<O> & typeof CODES } }
//// [b.ts]
import type { Opts } from "missing-core";
import { f } from "./a";
export async function g<C extends Opts>() { const r = f<C>(); return { ...r }; }
//// [c.ts]
import { f } from "./a";
export async function h() { const r = f(); return { ...r }; }


//// [a.js]
export function f() { return {}; }
//// [b.js]
import { f } from "./a";
export async function g() { const r = f(); return { ...r }; }
//// [c.js]
import { f } from "./a";
export async function h() { const r = f(); return { ...r }; }


//// [a.d.ts]
import type { Opts } from "missing-core";
import type { CODES } from "missing-core/error";
export declare function f<O extends Opts>(): {
    e: Partial<O> & typeof CODES;
};
//// [b.d.ts]
import type { Opts } from "missing-core";
export declare function g<C extends Opts>(): Promise<{
    e: Partial<O> & typeof Opts;
}>;
//// [c.d.ts]
export declare function h(): Promise<{
    e: Partial<O> & any;
}>;
