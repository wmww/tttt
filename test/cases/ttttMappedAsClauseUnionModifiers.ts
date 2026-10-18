// @strict: true
// @exactOptionalPropertyTypes: true
// An instantiated generic mapped type with an `as` clause takes its keys from its own modifiers type's apparent
// properties, a union's common ones too, not its generic declaration's expansion (xstate's ToStateSchema<TConfig>)
type StateSchema = { id?: string; states?: Record<string, StateSchema>; type?: unknown; };
type ToStateSchema<TSchema extends StateSchema> = {
  -readonly [K in keyof TSchema as K & ('id' | 'states')]: K extends 'states'
    ? { [SK in keyof TSchema['states']]: ToStateSchema<NonNullable<TSchema['states'][SK]>> }
    : TSchema[K];
};
interface Inner<C> { id?: string | undefined; ctx?: C; states?: Record<string, Inner<C>>; }
type Config<C> = Inner<C> & ({ version?: string } | { output?: C });
interface Box<S extends StateSchema> { s: S }
declare function g<C, const T extends Config<C>>(c: T): Box<ToStateSchema<T>>;
