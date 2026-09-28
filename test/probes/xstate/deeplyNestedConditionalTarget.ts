// A relation to an infinitely expanding recursive conditional type (xstate's StateId) is assumed to hold after ten
// nested instantiations on the target side (tsc's isDeeplyNestedType(target, targetStack, targetDepth, 10)): measuring
// the variance of Snap ran into TS2589, whose error types poisoned every property type computed meanwhile
type Values<T> = T[keyof T];
type StateSchema = {
  id?: string;
  route?: unknown;
  states?: Record<string, StateSchema>;
  type?: unknown;
};
type StateId<
  TSchema extends StateSchema,
  TKey extends string = '(machine)',
  TParentKey extends string | null = null
> =
  | (TSchema extends { id: string }
      ? TSchema['id']
      : TParentKey extends null
        ? TKey
        : `${TParentKey}.${TKey}`)
  | (TSchema['states'] extends Record<string, any>
      ? Values<{
          [K in keyof TSchema['states'] & string]: StateId<
            TSchema['states'][K],
            K,
            TParentKey extends string
              ? `${TParentKey}.${TKey}`
              : TSchema['id'] extends string
                ? TSchema['id']
                : TKey
          >;
        }>
      : never);
interface Snap<S extends StateSchema> {
  getMeta: () => StateId<S>;
}
declare function g<S extends StateSchema>(s: Snap<S>): void;
function f<S extends StateSchema>(s: Snap<S>) { g(s); }
