// @strict: true
interface Matcher<R, Ret> { _R: R; _Ret: Ret }
declare const tag: <R, Ret, Fn extends (_: R) => Ret>(
  ...pattern: [first: string, ...values: Array<string>, f: Fn]
) => (self: Matcher<R, Ret>) => Matcher<R, Ret>
const f: (self: Matcher<number, "a" | "b">) => Matcher<number, "a" | "b"> = tag("A", () => "a")
declare const tag2: <R, Ret, Fn extends (_: R) => Ret>(first: string, f: Fn) => (self: Matcher<R, Ret>) => Matcher<R, Ret>
const f2: (self: Matcher<number, "a" | "b">) => Matcher<number, "a" | "b"> = tag2("A", () => "a")
declare const tag3: <R, Ret, Fn extends (_: R) => Ret>(...pattern: [first: string, f: Fn]) => (self: Matcher<R, Ret>) => Matcher<R, Ret>
const f3: (self: Matcher<number, "a" | "b">) => Matcher<number, "a" | "b"> = tag3("A", () => "a")
