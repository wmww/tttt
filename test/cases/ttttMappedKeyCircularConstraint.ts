// A mapped type's key with a circular constraint (TS2313) has none: the mapped type's constraint is the error type
// (tsc's getConstraintTypeFromMappedType), not a generic mapped type T relates to by its `T[P]` template. Related to
// a target, the key is the unconstrained type parameter of TS2208, named where it's declared (mutations of
// mappedTypeAsClauseRelationships)
// @strict: true
type Filter<T> = { [P in keyof T[P] extends Function ? P : never]: T[P] };
type ModifyExclOpt<T> = { [P in keyof T extends string? `bool${P}`: never ]-?: T[P] };
function fun<T>(val: T) {
    let x: Filter<T> = val;
}
type Ok<T> = { [P in keyof T]: T[P] };
function fun2<T>(val: T) {
    let x: Ok<T> = val;
}
