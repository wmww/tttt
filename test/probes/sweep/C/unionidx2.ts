// @strict: true
type H = { setup(d: number): void } | { [key: string]: any };
const x: H = { setup(d) {} };
type H2 = { setup(d: number): void } | { a: number } | { [key: string]: any };
const y: H2 = { setup(d) {} };
