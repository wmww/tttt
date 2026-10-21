// @strict: true
// @noEmit: true
// A template literal type printed in a message escapes what tsgo's escapeStringWorker escapes for any quote: lone
// surrogates, NUL, U+0085, U+2028/2029
type Hi = "\uD83D";
type T1 = `${Hi}${Lo}`;
type T2 = `a\uD83D${number}`;
type T3 = `\uDE00${number}b\uD83D`;
const a: Hi = 1;
const b: T1 = 1;
const c: T2 = 1;
const d: T3 = 1;
const e: `x${Hi}` = 1;
const f: `\u{1F600}${number}` = 1;
const h: `\0${number}\u2028\x001\u0085` = 1;
const i: `\u0001${number}\r\n` = 1;
