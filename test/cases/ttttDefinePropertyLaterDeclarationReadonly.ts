// @allowJs: true
// @checkJs: true
// @noEmit: true

// A property any declaration of which is a read-only Object.defineProperty is read-only (tsc's isReadonlySymbol:
// some declaration, not the first), so its assignments are TS2540 too
// @filename: a.js
const B = {}
B.NS = 1;
Object.defineProperty(B, "NS", { writable: true });
const C = {}
C.x = 1;
Object.defineProperty(C, "x", { value: 2, writable: false });
C.x = 3;
const D = {}
Object.defineProperty(D, "y", { value: 2, writable: false });
D.y = 3;
// A descriptor without properties (never, any) has neither `value` nor `set`: read-only too
/** @type {never} */
const nd = /** @type {never} */ (null);
const E = {}
Object.defineProperty(E, "z", nd);
E.z = 1;
/** @type {any} */
const ad = {};
const F = {}
Object.defineProperty(F, "w", ad);
F.w = 1;
