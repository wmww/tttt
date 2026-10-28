// @allowJs: true
// @checkJs: true
// @noEmit: true
// @module: esnext
// @filename: a.js
const dec = 0, dec2 = 0;
@dec export @dec2 function f1() {}
@dec export @dec2 const x2 = 1;
@dec export function f5() {}
export @dec2 function f6() {}
// @filename: b.ts
declare const dec: any, dec2: any;
@dec export @dec2 function f1() {}
// @filename: c.js
function dec2() {}
@dec2 declare module "zz" {}
/** @type {number} */ const sentinel = "";
