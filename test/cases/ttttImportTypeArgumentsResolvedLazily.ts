// An import type's type arguments aren't checked (tsc's checkImportType), only resolved: what is in a type literal or a
// signature in there is deferred and says nothing, an unresolvable module included (@sentry/browser's feedbackAsync.d.ts
// in monkeytype's storybook)
// @filename: m.ts
export type F<T> = { t: T };
export interface I { i: 1 }
// @filename: b.ts
export declare const x: import("./m").F<import("./m").I & { a(o?: import("./nope").T): void; b: Missing; c: import("./nope2").U }>;
export declare const y: import("./m").F<Missing2>;
export declare const z: import("./m").F<import("./nope3").V>;
export declare const w: import("./m").F<Array<import("./nope4").V>>;
export declare const v: import("./m").F<(a: import("./nope5").V) => void>;
