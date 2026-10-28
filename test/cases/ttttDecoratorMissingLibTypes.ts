// @strict: true
// @noLib: true
// @target: es2022
// @experimentalDecorators: false,true
// A decorator's lib type that is missing (TS2318) is tsc's emptyGenericType: unknown for an ES decorator's context,
// target and result (tryCreateTypeReference), `{}` for the legacy descriptor (createTypeFromGenericGlobalType), and
// the call is checked all the same (bun 16-C7a, 16-C7b)
interface Array<T> { length: number } interface Boolean {} interface Function {} interface IArguments {} interface Number {} interface Object {} interface RegExp {} interface String {}
interface CallableFunction extends Function {} interface NewableFunction extends Function {}
declare function dec(target: any, context: { name: string }): void;
declare function dec3(target: any, key: any, d: { value: number }): void;
@dec class C {
  @dec accessor g = 1;
  @dec3 m() {}
}
