type V<T extends { ins: object }> = { [K in keyof T["ins"]]: T["ins"][K] | null } & {};
class B<T extends { ins: object }> {
    values(values: V<T> | V<T>[]) {
        values = Array.isArray(values) ? values : [values];
        values.length;
        const a = Array.isArray(values) ? values : [values];
        a.length;
    }
}
