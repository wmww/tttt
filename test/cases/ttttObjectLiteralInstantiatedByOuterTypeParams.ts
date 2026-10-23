// @strict: true
// @declaration: true
// An object literal's type is instantiated by every type parameter declared around it, whatever its properties
// mention (tsc's getObjectTypeInstantiation filters only a method's or type literal's by
// isTypeParameterPossiblyReferenced): two applications of a generic function make two `{ n: number }` types, which
// an intersection keeps both of. A type literal in an annotation mentions nothing and stays one type (feathers'
// initializeBaseContext() applied twice)
interface Base { language: 'js' | 'ts' }
declare const ctx: Base;
const init = <C extends Base>(ctx: C) => ({ ...ctx, n: 1 });
export const twice = init(init(ctx));
const plain = <C,>(c: C) => ({ n: 1 });
declare function both<A, B>(a: A, b: B): A & B;
export const x = both(plain(1), plain(""));
type L = { n: number };
const annotated = <C extends Base>(ctx: C): C & L => null!;
export const once = annotated(annotated(ctx));
