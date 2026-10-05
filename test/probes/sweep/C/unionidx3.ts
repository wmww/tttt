// @strict: true
type H = { noBubble: boolean } | { setup(d: number): void } | { [key: string]: any };
const x: H = { noBubble: true, setup(d) {} };
type H2 = { setup(d: number): void } | { [key: string]: any };
const y: H2 = { noBubble: true, setup(d) {} };
type H3 = { noBubble: boolean } | { setup(d: number): void };
const z: H3 = { noBubble: true, setup(d) {} };
