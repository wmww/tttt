// @target: es2022
// @importHelpers: true
// A bodiless accessor whose decorators are rejected (TS1206) isn't also '{' expected (bun case 34-C16): tsc's
// checkAccessorDeclaration runs checkGrammarAccessor only when checkGrammarFunctionLikeDeclaration said nothing. Nor
// does the decorated setter after it make the class need __setFunctionName: of an accessor pair only the first one with
// decorators counts (ClassElementOrClassElementParameterIsDecorated), and the bodiless getter can't be decorated
// @filename: node_modules/tslib/index.d.ts
export declare function __nothing(): void;
// @filename: node_modules/tslib/package.json
{ "name": "tslib", "version": "2.0.0", "types": "index.d.ts" }
// @filename: a.ts
export {};
declare const dec: any;
@dec class Q { @dec get x(): number; @dec set x(v: number) {} }
class R { get y(): number; set y(v: number) {} }
