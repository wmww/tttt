//// [test/cases/ttttDeclarationImportTypeQuotes.ts] ////

//// [c.ts]
export interface X { a: 1 }
//// [a.d.ts]
export declare function mk(): [{ $props: import('./c').X; n: number }];
//// [b.ts]
import { mk } from "./a";
const [p] = mk();
export { p };


//// [c.js]
export {};
//// [b.js]
import { mk } from "./a";
const [p] = mk();
export { p };


//// [c.d.ts]
export interface X {
    a: 1;
}
//// [b.d.ts]
declare const [p]: [{
    $props: import('./c').X;
    n: number;
}];
export { p };
