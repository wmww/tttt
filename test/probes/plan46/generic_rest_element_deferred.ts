// @strict: true
// @exactOptionalPropertyTypes: true
type CR<T, C extends boolean> = C extends true ? Required<T> : T;
type Sp = <L extends string>(src: L, ...[options]: CR<[options?: { id?: string; input?: L extends string ? unknown : number }], L extends 'x' ? true : false>) => void;
declare const spawn: Sp;
const f: Sp = (src, options) => { spawn(src, options); };
