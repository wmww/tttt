declare function g<D extends Record<string, number>>(k: keyof D): D;
const r = g("test");
declare function h<D>(k: keyof D): D;
const s = h("test");
