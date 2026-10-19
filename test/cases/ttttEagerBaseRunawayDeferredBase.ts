// tsgo's eager-base runaway (TS5115) through an interface whose base is a deferred alias reference (`J extends _J`,
// `type _J = Union<[Rec<J>]>`): cut down from zod v4's $ZodUnionInternals/$ZodRecordInternals/ZodJSONSchema
// @strict: true
type OptSchema = { _z: { opt: "o" | "d" } };
type IsOpt<T extends Some> = T extends OptSchema ? true : false;
interface BaseInternals { version: 1; opt?: "o" | "d" | undefined; pat: RegExp | undefined; }
interface Internals<out O = unknown, out I = unknown> extends BaseInternals { output: O; input: I }
type Some = { _z: BaseInternals };
interface Type<O = unknown, I = unknown, N extends Internals<O, I> = Internals<O, I>> { _z: N }
interface UnionInternals<T extends readonly Some[] = readonly Type[]> extends BaseInternals {
    output: unknown; input: unknown; pat: T[number]["_z"]["pat"];
    opt: IsOpt<T[number]> extends false ? "o" | "d" | undefined : "o" | "d";
}
interface Union<T extends readonly Some[] = readonly Type[]> extends Type<any, any, UnionInternals<T>> { _z: UnionInternals<T> }
type RecInput<V extends Some = Type> = [V] extends [OptSchema] ? Partial<Record<string, V>> : Record<string, V>;
type RecOutput<V extends Some = Type> = V extends OptSchema ? Partial<Record<string, V>> : Record<string, V>;
interface RecordInternals<V extends Some = Type> extends Internals<RecOutput<V>, RecInput<V>> { def: V }
interface Rec<V extends Some = Type> extends Type { _z: RecordInternals<V> }
type _J = Union<[Rec<J>]>;
type _JI = _J["_z"];
interface JI extends _JI { output: unknown }
interface J extends _J { _z: JI }
