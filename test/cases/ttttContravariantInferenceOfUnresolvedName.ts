// @strict: true
// A type parameter inferred only from contravariant return-type candidates is their intersection (tsc's
// getContravariantInference with a combining priority), and getIntersectionType of one any is the plain error type: an
// unresolved name's aliased any loses its name there (mockoon's rxjs `pipe(switchMap(...), tap(...))` printing
// `Observable<any>`), while a union of one keeps it
import { DI } from 'nonexistent';
declare class Obs<T> { _t: T; pipe<A, B>(op1: OF<T, A>, op2: OF<A, B>): Obs<B> }
interface UF<T, R> { (source: T): R }
interface OF<T, R> extends UF<Obs<T>, Obs<R>> {}
interface MOF<T> extends OF<T, T> {}
declare const dop: OF<number, DI>;
declare function tap<T>(x?: (value: T) => void): MOF<T>;
declare const src: Obs<number>;
export const s1 = src.pipe(dop, tap(() => {}));
export const s2 = src.pipe(dop, tap((v) => {}));
declare function same<T>(x: T, y: T): Obs<T>;
declare const di: DI;
export const s3 = same(di, di);
export const r1: string = s1;
export const r2: string = s2;
export const r3: string = s3;
