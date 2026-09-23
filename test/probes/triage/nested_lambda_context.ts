declare function foo<T, U>(x: T, fun: () => (x: T) => U, fun2: (y: U) => T): [T, U];
foo("", function () { return x => x.length }, p => undefined);
foo("", () => x => x.length, p => "");
