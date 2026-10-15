// @strict: true
// tsc types an object literal's getters lazily: one in a call's argument is first typed by the call's resolution, once
// the argument's contextual type is popped, so its `this` is the resolving signature's any (no TS2322 below). The
// deferred check of its body later types `this` with the resolved signature. A getter the resolution never asks for
// (`z`: no property of its name in the parameter type) is typed afterwards, with the resolved contextual type
interface Box<T> { value: T };
declare function h(b: Box<number>): void;
h({ get value() { return this; } });
declare function k(b: { value: number, w: string }): void;
k({ w: "", get value() { return this.w; } });
const o: Box<number> = { get value() { return this; } };
declare function g<T>(box: Box<T>): T
g({ get value() { return this; } });
declare function m<T>(b: { m(): T }): T;
const y = m({ m() { return 1; }, get z() { return this; } });
