// @strict: true
// Deep nesting's walk order (the shared graph's "newer" test, relate.c walk_newer()) cut ZodObject<...> three levels
// down where tsc's ids don't: each level's shape is `Writeable<{...}>`, a homomorphic mapped type over the object
// literal's type, so its property types are the literal's, made before it (youtube-enhancer's migrate-translation.ts:
// TS2578 at K > 1 only). made_before_step() now also looks through a key part made from an expression. Fails without
// the fix under the graph (K > 1, or TTTT_NESTING_ORDER=walk); one plain checker goes by ids
type Writeable<T> = { -readonly [P in keyof T]: T[P] } & {};
interface ZodType<T> { _t: T }
interface ZodOptional<T> { _o: T }
interface ZodArray<T> { _a: T }
interface ZodObject<Shape> { shape: Shape }
declare function object<T extends object>(shape: T): ZodObject<Writeable<T>>;
type Nullable<T> = null | T;
type TypeToZod<T> = {
    [K in keyof T]: T[K] extends boolean | Nullable<number> | string | undefined ?
        undefined extends T[K] ? ZodOptional<ZodType<Exclude<T[K], undefined>>> : ZodType<T[K]>
    : ZodObject<TypeToZod<T[K]>>;
};
type TypeToZodSchema<T> = ZodObject<{
    [K in keyof T]: T[K] extends any[] ? ZodArray<ZodType<T[K][number]>>
    : T[K] extends object ? ZodObject<TypeToZod<T[K]>>
    : ZodType<T[K]>;
}>;
type Old = { settings: { sections: { removingWatchedVideos: string } } };
// @ts-expect-error the inner object lacks removingWatchedVideos
export const schema: TypeToZodSchema<Old> = object({ settings: object({ sections: object({}) }) });
