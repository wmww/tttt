interface A { start: number }
interface I { [k: `${string}%`]: number }
declare function f(p: A | I): void;
f({ "0%": 1 });
const x: A | I = { "0%": 1 };
const y: string | { [k: string & {}]: number } = { "q": 1 };
