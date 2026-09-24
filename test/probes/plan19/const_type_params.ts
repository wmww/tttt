declare function q<const K extends readonly string[]>(k: K): K;
const s = q(['a', 'c']);
declare function id<const T>(t: T): T;
let r = id('foo');
let o = id({ a: 1, b: ['x', true] });
declare function w<const T>(x: { v: T }): T;
let v = w({ v: [1, 2] });
declare function u<const T extends string | number>(x: T[]): T;
let uu = u([1, 'a']);
declare function m<const T extends unknown[]>(...x: T): T;
let mm = m(1, 'a');
declare function n<T>(t: T): T;
let nn = n(['a']);
declare function k<const T extends Record<string, unknown>>(x: T): T;
let kk = k({ a: 'x', b: [1] });
class C<const T> { constructor(public t: T) {} }
let cc = new C({ x: 'y' });
