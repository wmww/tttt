// @allowJs: true
// @checkJs: true
// @noEmit: true
// A script's declaration that conflicts with another file's global is still its file's local to tsgo's binder: an
// expando assignment in the file declares into it (the literal is `{ d: {}; }`), the checker's `A` is the global
// @filename: a.d.ts
declare class A {}
declare var B: number;
// @filename: b.js
const A = { };
A.d = { };
function B() {}
B.e = 1;
// @filename: c.js
const D = {};
D.h = 1;
// @filename: d.js
const D = {};
D.i = 2;
