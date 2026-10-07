interface Bear { roars: boolean; }
declare function id<T>(y: T): T;
const a = id(null);
declare function f<T>(y: T & Bear): T;
const b = f(null);
declare function g<T>(y: T | Bear): T;
const c = g(null);
