// @strict: true
type K = keyof {1: boolean; a: 1};
declare const k: K;
type T2 = 1 extends keyof {1: boolean} ? "yes" : "no";
declare const t2: T2;
type T3 = 1 extends K ? "yes" : "no";
declare const t3: T3;
function f(x: keyof {1: boolean}) { const y: 1 = x; }
