declare function f(x: string): void;
declare function f(x: Function): void;
f(function() {});
declare function g(x: string): void;
declare function g(x: { a: Function }): void;
g({ a: function() {} });
