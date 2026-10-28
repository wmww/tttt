// @target: es2022
// An ES decorator context's `name` is tsc's getLiteralTypeFromPropertyName of the member's name: a numeric name's
// number literal, a computed one's literal type, a bigint's bigint (intersected with the context's `string | symbol`:
// never, assignable to anything: bun case 16-P2a, not here, tsgo's test harness writes no baseline for a bigint method
// name). See ttttLegacyDecoratorKeyType for a legacy decorator's key
declare function decN(target: any, context: { name: 0 }): void;
declare function decS(target: any, context: { name: "x" }): void;
class C {
  @decN [1]() {}
  @decN [2] = 1;
  @decN get [3]() { return 1 }
  @decS 4() {}
  @decS ["y"]() {}
}
