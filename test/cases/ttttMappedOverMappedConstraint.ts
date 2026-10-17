// @strict: true
// A homomorphic mapped type over T whose constraint is itself a generic mapped type has that mapped type's members
// (tsgo's resolveMappedTypeMembers over the apparent modifiers type): here a string index signature, through
// `Partial<{ [K in keyof D]: X }>` with `D extends Record<string, P<unknown>>`. No TS7053/TS2339, unknown not any
interface P<T> { validate(v: unknown): v is T }
type SchemaValue<X> = X extends P<infer T> ? T : never;
function createSchema<D extends Record<string, P<unknown>>>(definition: D) {
    return {
        validateOrDefault<T extends Partial<{ [K in keyof D]: SchemaValue<D[K]> }>>(values: { [K in keyof T]?: unknown } | undefined, defaults: T): T {
            const raw: { [K in keyof T]?: unknown } = values ?? {};
            for (const key of Object.keys(definition)) {
                const candidate = raw[key];
                const named = raw.foo;
            }
            return defaults;
        },
    };
}
