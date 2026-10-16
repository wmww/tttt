// The contextual type of a property of `T & V<T>`, V a conditional type whose constraint is the union of its branches,
// is what each alternative of the intersection distributed over that union says (tsc's getApparentTypeOfIntersectionType
// makes `(C & {}) | (C & M)`), not the intersection of C's property with the union's: the generic call there infers its
// type parameters from `(t: any) => ...` (ui's tanstack-table demos printed `Table<TableFeatures, any>` for
// `createFilteredRowModel()` in `tableFeatures({...})`)
// @strict: true
// @declaration: true
interface Feats { a?: number; m?: (t: any) => () => RowModel<any, any> }
interface Table<F extends Feats, D> { f: F; d: D }
interface RowModel<F extends Feats, D> { rf: F; rd: D }
declare function mk<F extends Feats, D = any>(): (t: Table<F, D>) => () => RowModel<F, D>;
type IsAny<T> = 0 extends 1 & T ? true : false;
type V<T extends Feats> = IsAny<T> extends true ? {} : { [K in keyof T as K extends 'm' ? K : never]: T[K] };
declare function take<T extends Feats>(x: T & V<T>): T;
export const features = take({ m: mk() });
