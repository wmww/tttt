// @declaration: true
// An `export { ... }` inside a declared namespace keeps the imports it names (tsc's aliasMarkingVisitor walks the
// whole file for export specifiers: typedoc's `export declare namespace JSX { export { IntrinsicElements } }`)
// @filename: m.ts
export interface A { a: number }
export interface B { b: number }
// @filename: index.ts
import type { A, B } from "./m";
export declare namespace JSX {
    export { A, B as Bee };
}
export declare namespace N2 { namespace Deep { export { B }; } }
