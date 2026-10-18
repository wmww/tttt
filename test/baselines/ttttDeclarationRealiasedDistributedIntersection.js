//// [test/cases/ttttDeclarationRealiasedDistributedIntersection.ts] ////

//// [t.ts]
type Base<I> = { readonly id: I };
export type Rec<I> = Base<I> & ({ readonly a: 1; s: I } | { readonly b: 2 });
type Stored = Rec<string>;
export declare function get(): Stored;
export declare function get2(): Rec<string>;

//// [a.ts]
import { get, get2 } from './t';
export const x = get();
export const y = get2();


//// [t.js]
export {};
//// [a.js]
import { get, get2 } from './t';
export const x = get();
export const y = get2();


//// [t.d.ts]
type Base<I> = {
    readonly id: I;
};
export type Rec<I> = Base<I> & ({
    readonly a: 1;
    s: I;
} | {
    readonly b: 2;
});
type Stored = Rec<string>;
export declare function get(): Stored;
export declare function get2(): Rec<string>;
export {};
//// [a.d.ts]
export declare const x: {
    readonly id: string;
} & ({
    readonly a: 1;
    s: string;
} | {
    readonly b: 2;
});
export declare const y: import("./t").Rec<string>;
