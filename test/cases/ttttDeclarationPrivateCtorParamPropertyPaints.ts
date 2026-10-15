// A private constructor's signature isn't written but its parameter properties are: their annotations name things,
// which paints the imports they go through (vscode's callHierarchy.d.ts lost `RefCountedDisposable`)
// @declaration: true
// @filename: /l.ts
export class Ref { release() {} }
// @filename: /c.ts
import { Ref } from "./l";
export class M {
    private constructor(readonly ref: Ref) {}
    static make() { return new M(new Ref()); }
}
