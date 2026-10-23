// @strict: true
// @noLib: true
// @noTypesAndSymbols: true
// A global `NonNullable` of another arity isn't the one narrowing instantiates (tsc's getGlobalTypeAliasSymbol checks
// the arity): `T & {}` (bun's 01-C12b aborted)
interface Array<T> {}
interface Object {}
interface Function {}
interface String {}
interface Number {}
interface Boolean {}
interface RegExp {}
interface IArguments {}
interface CallableFunction {}
interface NewableFunction {}
type NonNullable<T, U> = T;
function f3<T>(o: T) { if (o != null) { const n: number = o; } }
