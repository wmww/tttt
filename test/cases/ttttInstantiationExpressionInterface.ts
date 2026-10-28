// An instantiation expression is an ExpressionWithTypeArguments to tsc: a value use of an interface there is TS2689
// (getEntityNameForExtendingInterface), not TS2693 (mutation testing: reverseMappedUnionInference, awaitedTypeJQuery)
interface I<T> { a: T }
const x = I<string>;
namespace N { export interface J<T> { a: T } }
const y = N.J<string>;
const z = I;
const w = I<string>();
const v = new I<string>();
declare const o: { I: number };
const u = o.I<string>;
