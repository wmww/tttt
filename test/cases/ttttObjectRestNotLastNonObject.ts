// An object pattern's rest of a non-object is TS2700 whatever the grammar says about the element (not last: TS2462,
// an initializer: TS1186): tsc's getTypeForBindingElement, apart from checkGrammar
declare const u: undefined | number;
var { ...m, a } = u;
var { ...n = {} } = u;
var { x: { ...q } } = { x: 1 };
let o = undefined;
var { ...m2, a2 } = o;
