// @strict: true
// The excess property target of an optional property is its union without undefined, filtered through the union's
// origin like tsc's filterType: `In | undefined` with In a named union is reported as 'In', not 'F[] | F'
// (better-drizzle's `orderBy?: OrderByInput<...>`)
type F = { a?: 1 } & { b?: 2 };
type In = F | F[];
type Q = { orderBy?: In };
export const q: Q = { orderBy: { writer: 1 } };
type Q2 = { orderBy: In | null };
export const q2: Q2 = { orderBy: { writer: 1 } };
