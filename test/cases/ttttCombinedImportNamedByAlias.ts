// `import { Big } from "big.js"` of an interface that the `export =` value also has as a property is tsc's combined
// symbol, whose declared type is an interface of its own: the import names it (declaration emit writes `Big`, not
// TS4058/TS4023 "cannot be named"), while what the module's own declarations make is `import("big.js").Big`
// (ghostfolio's getSum)
// @declaration: true
// @esModuleInterop: true
// @module: esnext
// @moduleResolution: bundler
// @strictNullChecks: true
// @filename: /node_modules/@types/big.js/index.d.ts
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
// @filename: /node_modules/@types/big.js/package.json
{"name":"@types/big.js","version":"6.0.0","types":"index.d.ts"}
// @filename: /helper.ts
import { Big } from 'big.js';
export function f(x: Big[]) { return x[0]; }
export function g() { return new Big(0); }
declare const v: Big;
export const h = v;
declare const arr: Big[];
export const r1 = arr.reduce((a) => a, new Big(0));
