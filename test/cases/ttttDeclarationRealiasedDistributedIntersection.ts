// @declaration: true
// @strict: true
// `type Stored = Rec<string>` of `Rec<I> = Base<I> & (A | B)`: the distributed union goes by the new alias with the
// intersection still its origin (tsc's instantiateTypeWorker passes the alias to getIntersectionType), so where the
// alias isn't accessible it prints as written, `Base & (A | B)`, not distributed

// @filename: t.ts
type Base<I> = { readonly id: I };
export type Rec<I> = Base<I> & ({ readonly a: 1; s: I } | { readonly b: 2 });
type Stored = Rec<string>;
export declare function get(): Stored;
export declare function get2(): Rec<string>;

// @filename: a.ts
import { get, get2 } from './t';
export const x = get();
export const y = get2();
