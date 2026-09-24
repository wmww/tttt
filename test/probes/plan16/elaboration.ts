const f = (): string => 1;
const g: () => string = () => 1;
let x: { a: string } = { a: 1 };
function h(p: { a: string }) {}
h({ a: 1 });
declare function f(): string;
let s: string = "";
s = f;
