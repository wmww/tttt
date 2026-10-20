//// [test/cases/ttttCombinedImportNamedByAlias.ts] ////

//// [index.d.ts]
declare namespace Big {
    type BigSource = number | string | Big | bigint;
    interface BigConstructor {
        new(value: BigSource): Big;
        (value: BigSource): Big;
        readonly Big: BigConstructor;
    }
    interface Big {
        plus(n: BigSource): Big;
    }
}
declare const Big: Big.BigConstructor;
type Big = Big.Big;
export = Big;
export as namespace Big;
//// [package.json]
{"name":"@types/big.js","version":"6.0.0","types":"index.d.ts"}
//// [helper.ts]
import { Big } from 'big.js';
export function f(x: Big[]) { return x[0]; }
export function g() { return new Big(0); }
declare const v: Big;
export const h = v;
declare const arr: Big[];
export const r1 = arr.reduce((a) => a, new Big(0));


//// [helper.js]
import { Big } from 'big.js';
export function f(x) { return x[0]; }
export function g() { return new Big(0); }
export const h = v;
export const r1 = arr.reduce((a) => a, new Big(0));


//// [helper.d.ts]
import { Big } from 'big.js';
export declare function f(x: Big[]): Big;
export declare function g(): import("big.js").Big;
export declare const h: Big;
export declare const r1: Big;
