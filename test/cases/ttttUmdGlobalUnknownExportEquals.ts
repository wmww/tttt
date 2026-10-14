// A UMD global whose `export =` names an import that resolves to nothing: a `declare global` namespace of the name
// takes the global, the UMD alias keeps its own declaration (tsc's mergeSymbol gets the unknown symbol)
// @filename: foo.d.ts
export {};
declare global {
    namespace Bar { }
}
// @filename: bar.d.ts
import { Bar } from './nonexistent';
export = Bar;
export as namespace Bar;
