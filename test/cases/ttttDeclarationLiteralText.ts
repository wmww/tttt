// @declaration: true
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
