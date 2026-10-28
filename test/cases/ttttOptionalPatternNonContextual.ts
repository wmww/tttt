// @strict: true
// An optional binding pattern parameter of a context-sensitive function checked without a contextual signature has
// undefined in its type, and its elements are taken from that (tsc's assignNonContextualParameterTypes)
const f = function ({ a }?) {};
const g = ({ b }?) => {};
function h({ c }?) {}
const k = function ([x]?) {};
const m = ({ d = 1 }?, e?) => d;
const n: (o?: { p: number }) => void = function ({ p }?) {};
const q = function ({ r }?: { r: number }) {};
f(); g({ b: 1 }); k(); m(); n(); q();
