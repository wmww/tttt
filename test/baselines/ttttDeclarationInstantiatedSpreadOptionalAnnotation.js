//// [test/cases/ttttDeclarationInstantiatedSpreadOptionalAnnotation.ts] ////

//// [a.ts]
declare const sym: unique symbol;
export type Tagged<M extends string> = M & { _: typeof sym };
export type Opts<T> = {
  links: T[];
  transformer?: Tagged<'moved'>;
  kind?: 'x' | 'y';
  count?: number;
  shape?: { a: 1 };
};
//// [b.ts]
import type { Opts } from './a';
function copy<T>(o: Opts<T>) {
  return { ...o };
}
export function generic<R>(o: Opts<R>) {
  return copy(o);
}
export function concrete(o: Opts<number>) {
  return copy(o);
}


//// [a.js]
export {};
//// [b.js]
function copy(o) {
    return { ...o };
}
export function generic(o) {
    return copy(o);
}
export function concrete(o) {
    return copy(o);
}


//// [a.d.ts]
declare const sym: unique symbol;
export type Tagged<M extends string> = M & {
    _: typeof sym;
};
export type Opts<T> = {
    links: T[];
    transformer?: Tagged<'moved'>;
    kind?: 'x' | 'y';
    count?: number;
    shape?: {
        a: 1;
    };
};
export {};
//// [b.d.ts]
import type { Opts } from './a';
export declare function generic<R>(o: Opts<R>): {
    links: R[];
    transformer?: import("./a").Tagged<'moved'>;
    kind?: 'x' | 'y';
    count?: number;
    shape?: {
        a: 1;
    };
};
export declare function concrete(o: Opts<number>): {
    links: number[];
    transformer?: import("./a").Tagged<'moved'>;
    kind?: 'x' | 'y';
    count?: number;
    shape?: {
        a: 1;
    };
};
