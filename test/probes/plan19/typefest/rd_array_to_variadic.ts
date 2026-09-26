// @strict: true
type R1 = boolean[] extends readonly [...infer U] ? U : 'no';
type R2 = boolean[] extends readonly [...[], ...infer U] ? U : 'no';
type R3 = [...string[], number] extends readonly [...infer U, infer V] ? [U, V] : 'no';
type R4 = [number, ...string[], number] extends readonly [infer A, ...infer U] ? [A, U] : 'no';
