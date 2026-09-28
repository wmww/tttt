// @strict: true
// execa 9's `Result<O>` is `InstanceType<typeof CommonResult<false, O>> & ...`: an instantiation expression whose type
// arguments mention the alias's type parameters is instantiated with the alias (tttt left `typeof C<false, O>` as it
// was, so the conditional InstanceType<...> stayed deferred and `result.stderr` was any). tsc: TS2322 with
// 'C<false, string>' at v and v2, 'D<string>' at v3, 'string' at v4, v5 and v6, 'typeof D<string>' at v7 (tttt prints
// `{ new <O>(): D<O>; prototype: D<any>; }` for the last: instantiation expression display)
declare abstract class C<A extends boolean, O> { x: O; y: A }
type R<O> = InstanceType<typeof C<false, O>>;
declare const r: R<string>;
export const v: number = r;
type R2 = InstanceType<typeof C<false, string>>;
declare const r2: R2;
export const v2: number = r2;
declare class D<O> { x: O }
type R3<O> = InstanceType<typeof D<O>>;
declare const r3: R3<string>;
export const v3: number = r3;
export const v4: number = r3.x;
export const v5: number = r.x;
type T6<O> = typeof D<O>;
declare const r6: T6<string>;
export const v6: number = new r6().x;
declare const r7: typeof D<string>;
export const v7: number = r7;
