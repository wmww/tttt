interface A { start: number }
const a: string | { [k: string]: number } = { q: 1 };
const b: string | { [k: `${string}%`]: number } = { "q%": 1 };
const c: A | { [k: string]: number } = { q: 1 };
const d: number | { [k: string & {}]: number } = { q: 1 };
const e: { [k: string & {}]: number } = { q: 1 };
