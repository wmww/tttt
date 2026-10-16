// @declaration: true
// @strict: true
// @module: nodenext
// A named import of an `export =` variable's property (node's `import { Console } from 'node:console'`, whose module
// is `export = globalThis.console`) is a value only: in a type position the name means the global interface, so
// declaration emit doesn't keep the import for it (vitest's logger)
// @filename: m.d.ts
declare global {
    interface Foo { Foo: FooCtor; hi(): void }
    interface FooCtor { new(): Foo }
    var foo: Foo;
}
export = globalThis.foo;
// @filename: a.ts
import { Foo } from './m';
export class L {
    public c: Foo;
    constructor() { this.c = new Foo(); }
}
export const f = (c: Foo) => c;
// @filename: b.ts
import { Foo } from './m';
import { f } from './a';
export const g = f;
export const h = (x: Foo) => [f(x), new Foo()];
