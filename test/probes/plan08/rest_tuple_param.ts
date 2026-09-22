// @strict: true
interface N { n: 1 }
declare function or(...fs: ((n: N) => boolean)[]): (...args: [node: N]) => boolean;
declare const decls: N[];
const found = decls.find(or(x => true, x => false));
