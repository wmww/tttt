type KeyOf<TValue> =
  IsAny<TValue> extends true
    ? never
    : TValue extends readonly unknown[]
      ? number extends TValue['length']
        ? '$' // For arrays we use '$' as a wildcard
        : {
            [TKey in keyof TValue]: TKey extends `${infer TIndex extends number}`
              ? TIndex
              : never;
          }[number]
      : TValue extends Record<string, unknown>
        ? keyof TValue & (string | number)
        : never;

/**
 * Path type.
 */
type Path = readonly (string | number)[];

/**
 * Required path type.
 */
type RequiredPath = readonly [string | number, ...Path];

/**
 * Paths type.
 */
type Paths = readonly RequiredPath[];

/**
 * Required paths type.
 */
type RequiredPaths = readonly [RequiredPath, ...RequiredPath[]];

/**
 * Lazily evaluate only the last valid path segment based on the given value.
 */
type LazyPath<
  TValue,
  TPathToCheck extends Path,
  TValidPath extends Path = readonly [],
> =
  // If path to check is empty, return last valid path
  TPathToCheck extends readonly []
    ? TValidPath
    : // If first key of path to check is valid, continue with next key
      TPathToCheck extends readonly [
          infer TFirstKey extends KeyOf<TValue>,
          ...infer TPathRest extends Path,
        ]
      ? LazyPath<
          TFirstKey extends keyof TValue
            ? TValue[TFirstKey]
            : TFirstKey extends '$'
              ? TValue extends readonly unknown[]
                ? TValue[number]
                : never
              : never,
          TPathRest,
          readonly [...TValidPath, TFirstKey]
        >
      : // If current value has valid keys, return them
        IsNever<KeyOf<TValue>> extends false
        ? readonly [...TValidPath, KeyOf<TValue>]
        : // Otherwise, return only last valid path
          TValidPath;

/**
 * Returns the path if valid, otherwise the last possible valid path based on
 * the given value.
 */
type ValidPath<TValue, TPath extends RequiredPath> =
  TPath extends LazyPath<TValue, TPath> ? TPath : LazyPath<TValue, TPath>;

/**
 * Returns a valid path for any given path based on the given value.
 */
type ValidPaths<TValue, TPaths extends RequiredPaths> = {
  [TKey in keyof TPaths]: ValidPath<TValue, TPaths[TKey]>;
};

/**
 * Deeply picks specific keys.
 *
 * Hint: If this type is ever exported and accessible from the outside, it must
 * be wrapped in `UnionToIntersect` to avoid invalid results.
 */
type DeepPick<TValue, TPath extends Path> = TPath extends readonly [
  infer TFirstKey extends string | number,
  ...infer TPathRest extends Path,
]
  ? TValue extends readonly unknown[]
    ? number extends TValue['length']
      ? TPathRest extends readonly []
        ? TValue
        : DeepPick<TValue[number], TPathRest>[]
      : {
          [TKey in keyof TValue]: TKey extends `${TFirstKey}`
            ? TPathRest extends readonly []
              ? TValue[TKey]
              : DeepPick<TValue[TKey], TPathRest>
            : unknown;
        }
    : {
        [TKey in keyof TValue as TKey extends TFirstKey
          ? TKey
          : never]: TPathRest extends readonly []
          ? TValue[TKey]
          : DeepPick<TValue[TKey], TPathRest>;
      }
  : never;

/**
 * Deeply merges two types.
 */
type DeepMerge<TValue1, TValue2> = TValue1 extends readonly unknown[]
  ? TValue2 extends readonly unknown[]
    ? number extends TValue1['length'] | TValue2['length']
      ? DeepMerge<TValue1[number], TValue2[number]>[]
      : {
          [TKey in keyof TValue1]: TKey extends keyof TValue2
            ? unknown extends TValue1[TKey]
              ? TValue2[TKey]
              : TValue1[TKey]
            : never;
        }
    : never
  : TValue1 extends Record<string, unknown>
    ? TValue2 extends Record<string, unknown>
      ? {
          [TKey in keyof (TValue1 & TValue2)]: TKey extends keyof TValue1
            ? TKey extends keyof TValue2
              ? DeepMerge<TValue1[TKey], TValue2[TKey]>
              : TValue1[TKey]
            : TKey extends keyof TValue2
              ? TValue2[TKey]
              : never;
        }
      : never
    : TValue1 & TValue2;

/**
 * Deeply picks N specific keys.
 */
type DeepPickN<TInput, TPaths extends Paths> = TPaths extends readonly [
  infer TFirstPath extends Path,
  ...infer TRestPaths extends Paths,
]
  ? TRestPaths extends readonly []
    ? DeepPick<TInput, TFirstPath>
    : DeepMerge<DeepPick<TInput, TFirstPath>, DeepPickN<TInput, TRestPaths>>
  : TInput;
type IsAny<T> = 0 extends 1 & T ? true : false;
type IsNever<T> = [T] extends [never] ? true : false;
type PartialInput = Record<string, unknown> | ArrayLike<unknown>;

declare let vp: ValidPaths<any, any>;
const p1: Paths = vp;
declare let dp: DeepPickN<any, any>;
const p2: PartialInput = dp;
declare let f1: (input: any) => boolean;
const p3: (input: PartialInput) => boolean = f1;
declare function g(paths: Paths, req: (input: PartialInput) => boolean): void;
const p4: (paths: ValidPaths<any, any>, req: (input: any) => boolean) => void = g;
