// @strict: true
declare class Sub { _: { a: 1 }; q: number }
declare class View { _: { b: 2 }; r: string }
function f<T extends Sub | Record<string, unknown> | View>(subquery: T) {
    const a = { ...subquery['_' as keyof typeof subquery] };
    const b = subquery['_' as keyof typeof subquery];
    return a;
}
