// @strict: true
// @noEmit: true
// An ambient destructuring declaration without an initializer is no grammar error (TS1182 is not for ambient ones),
// but its elements are typed by the pattern alone: TS7031 for each (tsc's getWidenedTypeForVariableLikeDeclaration)
declare const { r: { f: g }, s };
declare let [h];
declare var { t };
declare namespace N { const { u }: { u: number }; let { v }; }
