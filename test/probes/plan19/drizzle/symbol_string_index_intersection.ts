// @strict: true
// @target: es2015
function h<T extends Record<string, number>>(x: T[string | number | symbol], y: T[symbol]) { const n: number = x; const m: number = y; }
function h2<T extends Record<string, number> & Record<string, number | string>>(x: T[symbol]) { const n: number = x; }
function h3<T extends { a: 1 }, U extends T & Record<string, number>>(x: U[symbol]) { const n: number = x; }
