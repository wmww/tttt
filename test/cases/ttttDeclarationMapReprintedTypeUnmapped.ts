// @declaration: true
// @declarationMap: true
// @strict: true
// A conditional or object type a declaration's emit printed before is tsgo's cached clone (serializedTypes): its
// reused annotation nodes (here `infer T extends object`) are no longer mapped (trpc's middleware.d.ts.map)
declare function factory<A, B>(): { create: (a: A, b: B) => [A, B] };
export const make = <TCtx extends { ctx?: object; meta?: object }>() => ({
    create: factory<
        TCtx extends { ctx: infer T extends object } ? T : any,
        TCtx extends { meta: infer T extends object } ? T : object
    >(),
});
