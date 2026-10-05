type RI<T> = { [P in keyof T as P]: T[P] };
type Plain<T> = { [P in keyof T]: T[P] };
function f<T>(a: Plain<keyof T>, b: RI<keyof T>, c: Plain<`a${keyof T & string}`>) { return [a, b, c]; }
