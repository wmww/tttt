// @strict: true
type A = { [key: string]: any };
const a: A = { m(d) {} };
type B = { noBubble: boolean } | { [key: string]: any };
const b: B = { noBubble: true, m(d) {} };
