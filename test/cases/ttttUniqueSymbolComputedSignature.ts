declare const k: unique symbol;
declare const a: { readonly [k]: unique symbol };
declare const a2: { readonly x: unique symbol };
interface I { readonly [k]: unique symbol }
declare class Q { static readonly [k]: unique symbol }
interface J { [k]: unique symbol }
type L = { [k]: unique symbol[] };
declare const b: { readonly [k]: unique symbol };
class R { static readonly [k] = Symbol(); static readonly ["s"]: unique symbol; }
