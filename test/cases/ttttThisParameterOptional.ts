type T = (this?: any) => void;
type U = (this?: any, x: number) => void;
function f(this?: any) {}
let g = function (this?: any) {};
type V = (this = 1) => void;
type W = (this?) => void;
type X = (this: any, y?: any) => void;
type Y = (this) => void;
