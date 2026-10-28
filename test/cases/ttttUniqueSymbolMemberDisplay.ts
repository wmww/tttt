declare const k: unique symbol;
declare const b: { readonly x: unique symbol };
const c: { readonly x: unique symbol } = b;
declare const b2: { readonly [k]: unique symbol };
const c2: { readonly [k]: unique symbol } = b2;
