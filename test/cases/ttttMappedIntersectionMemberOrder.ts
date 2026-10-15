// @declaration: true
// @strict: true
// A mapped type over an intersection lists its properties in the order of their first declarations (tsgo's
// getNamedMembers sorts by compareSymbols): an intersection property is declared by every member's, so the first
// member's declaration-less mapped property (`[K in requiredKeys<T>]`) doesn't make it sort by name (zod's flatten)
type requiredKeys<T> = { [k in keyof T]: undefined extends T[k] ? never : k }[keyof T];
type optionalKeys<T> = { [k in keyof T]: undefined extends T[k] ? k : never }[keyof T];
type addQuestionMarks<T> = { [K in requiredKeys<T>]: T[K] } & { [K in optionalKeys<T>]?: T[K] } & { [k in keyof T]?: unknown };
type flatten<T> = { [k in keyof T]: T[k] };
declare function obj<T>(t: T): { [k in keyof addQuestionMarks<T>]: addQuestionMarks<T>[k] };
export const x = obj({ title: "a", body: "b", zeta: undefined as string | undefined, alpha: 1 });
