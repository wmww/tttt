const a: [...string[], 'suffix'] = ['item1', 'item2', 'suffix'];
const b: [number, ...string[], 'x'] = [1, 'a', 'b', 'x'];
const c: [...string[], 'x', 'y'] = ['x', 'y'];
declare function f<T extends unknown[]>(x: [...T, 'end']): T;
const d = f(['a', 1, 'end']);
