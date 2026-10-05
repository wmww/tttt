interface A { start: number }
interface I { [k: `${string}%`]: number }
const x: A | I = { "0%": 1 };
