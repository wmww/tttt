// @strict: true
// @exactOptionalPropertyTypes: true
// @target: es2022
// An assignment to a property whose type holds the missing type is TS2412 whether or not the property is optional (tsc's
// checkAssignmentOperator asks isExactOptionalPropertyMismatch of the property's type): an accessor whose getter returns
// an optional property's read
interface D { kind: "s"; a?: boolean }
class Inv {
  private _d?: D;
  get d() { return this._d }
  set d(v: typeof this._d) { this._d = v }
}
declare const inv: Inv;
declare const src: { kind: "s"; a: boolean | undefined } | undefined;
inv.d = src;
class Inv2 {
  private _d?: D;
  get d() { return this._d }
  set d(v) { this._d = v }
}
declare const inv2: Inv2;
inv2.d = src;
declare const o: { x?: D };
const r = o.x;
let q: typeof o.x = src;
