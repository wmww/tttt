// @target: es6
// @strict: true
// An arrow function whose returned value would fit as an async function's gets TS1356's suggestion: its return type
// is wrapped as tsc's createPromiseType does, awaited first (`"a" | Promise<"a">` makes Promise<"a">)
declare function g(cb: (v: boolean) => 42 | PromiseLike<"contextuallyTypable">): void;
g(v => v ? "contextuallyTypable" : Promise.reject());
g(async v => v ? "contextuallyTypable" : Promise.reject());
declare function h(cb: () => PromiseLike<string>): void;
h(() => Math.random() ? "a" : Promise.resolve("b"));
