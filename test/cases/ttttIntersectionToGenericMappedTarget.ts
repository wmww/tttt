// A generic mapped target `{ [K in keyof U]?: X }` relates only a mapped source by tsc's rules (mappedTypeRelatedTo,
// or S[K] against the template): an intersection whose members all fail it is not related as a whole by its (no)
// members. tttt's member-by-member fallback for generic sources passed it, which made better-drizzle's variances
// independent and hid a TS2502 behind a 2M-type runaway (source sweep refresh 5)
// @strict: true
function f<T, U>(
    a: { x?: number } & { [K in Exclude<keyof T, "y">]?: string },
    b: { [K in keyof T]?: T[K] } & { [K in keyof U]?: boolean },
) {
    const d: { [K in keyof U]?: number } = b;
    const e: { [K in Exclude<keyof T, "y">]?: string } = a;
    const g: { [K in keyof U]?: boolean } = b;
}
