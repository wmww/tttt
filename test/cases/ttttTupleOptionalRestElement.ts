// @strict: true
// `...a?: T[]` (TS5085) is an optional element of T, and alone in its tuple the array T[] (bun's 13-C11 crashed:
// the optional element's undefined was added to the rest array forever)
declare let t1: [...a?: string[]];
const n1: never = t1;
declare let t2: [x: number, ...a?: string[]];
const n2: never = t2;
const n3: never = t2[1];
