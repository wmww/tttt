//// [test/cases/ttttDeclarationUnknownImportRenamed.ts] ////

//// [a.ts]
import type { Opts } from "missing-mod";
import type { Adapter } from "missing-mod/adapter";
export const make = (fn: (h: { adapter: () => Promise<Adapter<Opts>>; get: () => Opts }) => void) => fn;
//// [b.ts]
import type { Plugin } from "missing-mod";
import { make } from "./a";
export const suite = make((h) => {});
export const g = (x: Parameters<Parameters<typeof make>[0]>[0]) => x;
//// [c.ts]
import { make } from "./a";
export const suite2 = make((h) => {});


//// [a.js]
export const make = (fn) => fn;
//// [b.js]
import { make } from "./a";
export const suite = make((h) => { });
export const g = (x) => x;
//// [c.js]
import { make } from "./a";
export const suite2 = make((h) => { });


//// [a.d.ts]
import type { Opts } from "missing-mod";
import type { Adapter } from "missing-mod/adapter";
export declare const make: (fn: (h: {
    adapter: () => Promise<Adapter<Opts>>;
    get: () => Opts;
}) => void) => (h: {
    adapter: () => Promise<Adapter<Opts>>;
    get: () => Opts;
}) => void;
//// [b.d.ts]
import type { Plugin } from "missing-mod";
import { make } from "./a";
export declare const suite: (h: {
    adapter: () => Promise<Plugin<Plugin>>;
    get: () => Plugin;
}) => void;
export declare const g: (x: Parameters<Parameters<typeof make>[0]>[0]) => {
    adapter: () => Promise<Plugin<Plugin>>;
    get: () => Plugin;
};
//// [c.d.ts]
export declare const suite2: (h: {
    adapter: () => Promise<Adapter<Opts>>;
    get: () => Opts;
}) => void;
