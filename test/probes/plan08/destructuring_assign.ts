// @strict: true
declare function pair(): [number, string];
declare const obj: { a: number, b: string };
let x: number, y: string, z: number;
[x, y] = pair();
({ a: z, b: y } = obj);
({ a: x } = obj);
let a: number, b: string;
({ a, b } = obj);
