// A conditional's `infer P` from a parameter typed by an alias of an intersection gets the intersection without the
// alias (tsc's getTypeFromInference: getIntersectionType of the lone contra candidate flattens it): `u` prints the
// members, not `WithChildren<{...}>` (discord.js ui's stories: `IAlert & { children?: ... }`)
// @strict: true
// @declaration: true
type WithChildren<P> = P & { children?: string };
declare function Alert(p: WithChildren<{ title: string }>): void;
declare function pick<T>(f: T): T extends (p: infer P) => any ? P : never;
export const u = pick(Alert);
