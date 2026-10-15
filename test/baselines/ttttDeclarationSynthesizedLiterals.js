//// [test/cases/ttttDeclarationSynthesizedLiterals.ts] ////

//// [ttttDeclarationSynthesizedLiterals.ts]
// The literals declaration emit makes (createLiteralConstValue: an enum member's, a readonly field's, export default's
// value) are ASCII-escaped; a literal expression's clone (pseudo types) escapes its value again keeping its quotes
export const colors = { reset: "\x1b[0m", e: "é", t: `\x1b` } as const;
export enum E { Shrug = "¯\\_(ツ)_/¯", X = "\x1b" }
export class C { readonly a = "\x1b"; static readonly b = 'é'; }
export const arr = ["\x1b", 'é'] as const;
export function f() { return "\x1b" as const; }
export let g: { x: "\x1b" } = { x: "\x1b" };
export default "\xe9\x1b";


//// [ttttDeclarationSynthesizedLiterals.js]
// The literals declaration emit makes (createLiteralConstValue: an enum member's, a readonly field's, export default's
// value) are ASCII-escaped; a literal expression's clone (pseudo types) escapes its value again keeping its quotes
export const colors = { reset: "\x1b[0m", e: "é", t: `\x1b` };
export var E;
(function (E) {
    E["Shrug"] = "\u00AF\\_(\u30C4)_/\u00AF";
    E["X"] = "\u001B";
})(E || (E = {}));
export class C {
    a = "\x1b";
    static b = 'é';
}
export const arr = ["\x1b", 'é'];
export function f() { return "\x1b"; }
export let g = { x: "\x1b" };
export default "\xe9\x1b";


//// [ttttDeclarationSynthesizedLiterals.d.ts]
export declare const colors: {
    readonly reset: "\u001B[0m";
    readonly e: "é";
    readonly t: `\u001B`;
};
export declare enum E {
    Shrug = "\u00AF\\_(\u30C4)_/\u00AF",
    X = "\u001B"
}
export declare class C {
    readonly a = "\u001B";
    static readonly b = "\u00E9";
}
export declare const arr: readonly ["\u001B", 'é'];
export declare function f(): "\u001B";
export declare let g: {
    x: "\x1b";
};
declare const _default = "\u00E9\u001B";
export default _default;
