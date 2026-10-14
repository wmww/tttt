// @allowJs: true
// @checkJs: true
// @noEmit: true
// @filename: a.js

// An assignment to a static member a class expression declares isn't an expando declaration (tsgo's
// bindDeferredExpandoAssignment declares only names nothing but expandos declare, in the initializer's symbol): the
// member's annotation is the assigned value's contextual type, like for a class declaration's statics.
var ns = {};
ns.sub = class {
    /** @type {(y: number) => void} */
    static cb;
};
ns.sub.cb = y => { y; };
ns.sub.zz = 1;
ns.sub.zz;

var K = class {
    /** @type {(x: number) => void} */
    static cb;
};
K.cb = x => { x; };
K.other = 2;
K.other;

class L {
    /** @type {(w: number) => void} */
    static cb;
}
L.cb = w => { w; };
L.inner = class {
    /** @type {(u: number) => void} */
    static cb;
};
L.inner.cb = u => { u; };

// Nested expandos still declare, without a contextual type from an earlier assignment's annotation
var o = {};
o.p = {};
o.p.r = /** @type {(a: number) => void} */ (undefined);
o.p.r = a => { a; };
