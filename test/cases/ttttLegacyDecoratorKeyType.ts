// @target: es2022
// @experimentalDecorators: true
// A legacy decorator's key argument is tsc's getClassElementPropertyKeyType: a numeric name's text, a computed one's
// string (or symbol), a bigint's the error type (no complaint)
declare function legacyN(target: any, key: number): any;
declare function legacyS(target: any, key: "1"): any;
class C {
  @legacyN 2n() {}
  @legacyS 1() {}
  @legacyN [3]() {}
  @legacyS 4() {}
}
