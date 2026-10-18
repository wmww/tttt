//// [test/cases/ttttDeclarationIndexSignatureBrandedKey.ts] ////

//// [ttttDeclarationIndexSignatureBrandedKey.ts]
// A mapped type over an aliased branded key: tsc's getLowerBoundOfKeyType makes the intersection again without its
// alias, so the index signature's key prints structurally; the emitter indents index signature parameters
// (LFIndexSignatureParameters), a multi-line key's members one level deeper
type CategorySlug = string & { readonly __brand: 'CategorySlug' };
export declare function g<K extends string>(x: K): { a: { [P in K]: 1 } };
export const z = g(null as any as CategorySlug);
export type Q = { [x: string & { readonly b: 1 }]: 1 };
export const q = null as any as { [x: string & { readonly b: 1 }]: 1 };
export interface I { [x: string & { readonly b: 1 }]: 1 }


//// [ttttDeclarationIndexSignatureBrandedKey.js]
export const z = g(null);
export const q = null;


//// [ttttDeclarationIndexSignatureBrandedKey.d.ts]
export declare function g<K extends string>(x: K): {
    a: {
        [P in K]: 1;
    };
};
export declare const z: {
    a: {
        [x: string & {
                readonly __brand: 'CategorySlug';
            }]: 1;
    };
};
export type Q = {
    [x: string & {
            readonly b: 1;
        }]: 1;
};
export declare const q: {
    [x: string & {
            readonly b: 1;
        }]: 1;
};
export interface I {
    [x: string & {
            readonly b: 1;
        }]: 1;
}
