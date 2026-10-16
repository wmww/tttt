//// [test/cases/ttttDeclarationDeferredIterableTypeArgs.ts] ////

//// [ttttDeclarationDeferredIterableTypeArgs.ts]
// `AsyncIterable<string, void, any>` prints without its trailing default arguments (tsc does that for four lib types)
// unless it's a deferred reference whose node writes them all, here one in a type alias's conditional type: tsgo's
// typeReferenceToTypeNode looks at the reference's node (trpc's subscription output printed `AsyncIterable<string, void>`)
type Inf<T> = T extends AsyncIterable<infer Y, infer R, infer N> ? { y: Y; r: R; n: N } : never;
type Out<T> = T extends AsyncIterable<any> ? AsyncIterable<Inf<T>['y'], Inf<T>['r'], Inf<T>['n']> : never;
declare function sub<O extends AsyncIterable<any, void, any>>(f: () => O): { output: Out<O> };
export const generator = sub(async function* () { yield 'data'; });
declare const g: AsyncIterable<string, void, any>;
export const plain = g;


//// [ttttDeclarationDeferredIterableTypeArgs.js]
export const generator = sub(async function* () { yield 'data'; });
export const plain = g;


//// [ttttDeclarationDeferredIterableTypeArgs.d.ts]
export declare const generator: {
    output: AsyncIterable<string, void, any>;
};
export declare const plain: AsyncIterable<string, void>;
