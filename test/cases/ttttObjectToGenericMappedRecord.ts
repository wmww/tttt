// @strict: true
// @target: es2022
// An object type whose properties don't depend on its type variables isn't related to a mapped type over a type
// parameter: `{ values: M }` against `Record<M, M>` (tttt's member fallback passed it). Flow unions reduce by subtype,
// so sequelize's `EnumOptions<Member>` was lost from `first` after an `if` (its @ts-expect-error then unused)
type EnumValues<M extends string> = readonly M[] | Record<M, M>;
interface EnumOptions<M extends string> { values: EnumValues<M> }
interface P<M> { values: M }
export function f<M extends string>(p: P<M>, eo: EnumOptions<M>) {
    const r1: Record<M, M> = p;
    const r2: Record<M, M> = eo;
}
export function g<Member extends string>(first: EnumValues<Member> | EnumOptions<Member>) {
    if (!Array.isArray(first) && 'values' in first) {
        if (typeof first.values !== 'string') { }
    }
    const n: number = first;
}
