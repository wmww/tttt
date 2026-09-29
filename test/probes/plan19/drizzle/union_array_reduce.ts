// @strict: true
// @target: es2015
declare const s: string;
const words = s.match(/a/g) ?? [];
const r = words.reduce((acc, word, i) => acc + word + i, '');
declare const u: string[] | number[];
const r2 = u.reduce((acc, x) => acc + String(x), '');
declare const m: RegExpMatchArray | string[];
const r3 = m.reduce((acc, x) => acc + x, '');
