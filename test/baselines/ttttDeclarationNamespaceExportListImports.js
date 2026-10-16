//// [test/cases/ttttDeclarationNamespaceExportListImports.ts] ////

//// [m.ts]
export interface A { a: number }
export interface B { b: number }
//// [index.ts]
import type { A, B } from "./m";
export declare namespace JSX {
    export { A, B as Bee };
}
export declare namespace N2 { namespace Deep { export { B }; } }


//// [m.js]
export {};
//// [index.js]
export {};


//// [m.d.ts]
export interface A {
    a: number;
}
export interface B {
    b: number;
}
//// [index.d.ts]
import type { A, B } from "./m";
export declare namespace JSX {
    export { A, B as Bee };
}
export declare namespace N2 {
    namespace Deep {
        export { B };
    }
}
