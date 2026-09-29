// @strict: true
// An inferred type argument is checked against its constraint with the inference as the constraint's `this` (tsc's
// getInferredType, getTypeWithThisArgument): drizzle's `T extends PgSelectQueryBuilder` of a `$dynamic()` builder
interface Builder {
    kind: string;
    same: (other: this) => boolean;
}
interface Special extends Builder {
    kind: "special";
    extra: number;
}
declare const special: Special;
const direct: Builder = special;
declare function withFriends<T extends Builder>(qb: T): T;
const inferred = withFriends(special);
