// @strict: true
declare function P<T extends object>(t: T, h: { get?(target: T, p: string): any }): T;
function wrap<T extends object>(target: T) {
    return P(target, {
        get(target, p) {
            return wrap(target);
        },
    });
}
declare function Q<T extends object>(t: T, h: { get?(target: T, p: string): any }): T;
function wrap2(target: object) {
    return Q(target, {
        get(target, p) {
            return wrap2(target);
        },
    });
}
