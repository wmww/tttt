type Tuple<T> = readonly [T, ...T[]];
type X = Tuple<"a">[`${number}`];
type Y = string[][`${number}`];
function f<T extends Tuple<"a">>(x: T[`${number}`]) { let y: "a" = x; }
