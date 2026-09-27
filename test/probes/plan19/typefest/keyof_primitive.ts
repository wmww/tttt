type A = keyof number;
type B = keyof 'x';
type C = `${Extract<keyof number, string>}`;
declare const a: A;
const s: string = a;
