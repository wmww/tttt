//// [test/cases/ttttDeclarationLiteralText.ts] ////

//// [ttttDeclarationLiteralText.ts]
// A const's literal initializer is a string literal the emitter makes: ASCII-escaped. A literal the declaration
// transformer visits prints as written, a template literal type's parts too (its raw text where the node builder
// reuses it)
export const zeroWidth = "\u{200B}";
export const noBreak = '\xa0';
export const accented = "é";
type Digits = "१" | 'é' | "\x41";
export type Message<c extends string> = `\\${c} like regex('\n') é A ${Digits}`;
export type Plain = `a\tb`;
export function f(x: "१", y: `x${number}\n`): void {}
export let v: Digits = "१";


//// [ttttDeclarationLiteralText.js]
// A const's literal initializer is a string literal the emitter makes: ASCII-escaped. A literal the declaration
// transformer visits prints as written, a template literal type's parts too (its raw text where the node builder
// reuses it)
export const zeroWidth = "\u{200B}";
export const noBreak = '\xa0';
export const accented = "é";
export function f(x, y) { }
export let v = "१";


//// [ttttDeclarationLiteralText.d.ts]
export declare const zeroWidth = "\u200B";
export declare const noBreak = "\u00A0";
export declare const accented = "\u00E9";
type Digits = "१" | 'é' | "\x41";
export type Message<c extends string> = `\\${c} like regex('\n') é A ${Digits}`;
export type Plain = `a\tb`;
export declare function f(x: "१", y: `x${number}\n`): void;
export declare let v: Digits;
export {};
