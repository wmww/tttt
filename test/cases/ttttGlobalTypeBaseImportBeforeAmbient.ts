// @module: commonjs
// @strict: true
// tsc's initializeTypeChecker asks for its global types (Array, Function...) before the scripts' ambient modules are in
// the globals: a `declare global` declaration of a thisless one (Function: no lib declaration mentions `this`) whose
// base goes through an import resolves that import then, and an ambient module can't be it (DefinitelyTyped's
// ember/v2). Array's lib declarations mention `this`, so its augmentation's base isn't looked at then
// @filename: /node_modules/@types/ext/index.d.ts
declare module "ext" {
    interface FunctionExt { property(): void }
    interface ArrayExt<T> { firstObject: T }
    const value: number;
}
// @filename: /a.ts
import * as Ext from "ext";
declare global {
    interface Function extends Ext.FunctionExt {}
}
(function () {}).property();
Ext.value;
// @filename: /b.ts
import * as Ext2 from "ext";
declare global {
    interface Array<T> extends Ext2.ArrayExt<T> {}
}
[1].firstObject;
Ext2.value;
