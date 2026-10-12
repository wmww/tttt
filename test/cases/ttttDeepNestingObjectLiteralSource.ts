// @strict: true
// Deep nesting (tsc's isDeeplyNestedType) has to stop on object literal sources too: inference into the return type of
// defu (its declarations below, verbatim) from the contextual type of `route = defu(route, ...)` walked the recursive
// Defu/MergeObjects/Merge aliases level after level otherwise: 500k types and a spurious TS2590 (nitro's vercel preset;
// fixed in 17dabf96, which also fixed nuxt's packages/schema/src/config/app.ts, not cut down here)
type Input = Record<string | number | symbol, any>;
type IgnoredInput = boolean | number | null | any[] | Record<never, any> | undefined;
type Merger = <T extends Input, K extends keyof T>(object: T, key: keyof T, value: T[K], namespace: string) => any;
type nullish = null | undefined | void;
type MergeObjects<Destination extends Input, Defaults extends Input> = Destination extends Defaults ? Destination : Omit<Destination, keyof Destination & keyof Defaults> & Omit<Defaults, keyof Destination & keyof Defaults> & {
    -readonly [Key in keyof Destination & keyof Defaults]: Destination[Key] extends nullish ? Defaults[Key] extends nullish ? nullish : Defaults[Key] : Defaults[Key] extends nullish ? Destination[Key] : Merge<Destination[Key], Defaults[Key]>;
};
type Defu<S extends Input, D extends Array<Input | IgnoredInput>> = D extends [
    infer F,
    ...infer Rest
] ? F extends Input ? Rest extends Array<Input | IgnoredInput> ? Defu<MergeObjects<S, F>, Rest> : MergeObjects<S, F> : F extends IgnoredInput ? Rest extends Array<Input | IgnoredInput> ? Defu<S, Rest> : S : S : S;
type DefuFn = <Source extends Input, Defaults extends Array<Input | IgnoredInput>>(source: Source, ...defaults: Defaults) => Defu<Source, Defaults>;
interface DefuInstance {
    <Source extends Input, Defaults extends Array<Input | IgnoredInput>>(source: Source | IgnoredInput, ...defaults: Defaults): Defu<Source, Defaults>;
    fn: DefuFn;
    arrayFn: DefuFn;
    extend(merger?: Merger): DefuFn;
}
type MergeArrays<Destination, Source> = Destination extends Array<infer DestinationType> ? Source extends Array<infer SourceType> ? Array<DestinationType | SourceType> : Source | Array<DestinationType> : Source | Destination;
type Merge<Destination extends Input, Defaults extends Input> = Destination extends nullish ? Defaults extends nullish ? nullish : Defaults : Defaults extends nullish ? Destination : Destination extends Array<any> ? Defaults extends Array<any> ? MergeArrays<Destination, Defaults> : Destination | Defaults : Destination extends Function ? Destination | Defaults : Destination extends RegExp ? Destination | Defaults : Destination extends Promise<any> ? Destination | Defaults : Defaults extends Function ? Destination | Defaults : Defaults extends RegExp ? Destination | Defaults : Defaults extends Promise<any> ? Destination | Defaults : Destination extends Input ? Defaults extends Input ? MergeObjects<Destination, Defaults> : Destination | Defaults : Destination | Defaults;

declare const defu: DefuInstance;


export {};
declare const src: string;
declare const status: number | undefined;
declare const headers: Record<string, string>;
function vercelRedirect() {
    let route = {
        src,
        status,
        headers: { Location: src },
    };
    route = defu(route, { headers });
    return route;
}

