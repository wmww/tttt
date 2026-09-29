// @strict: true
// @target: es2015
function wrap<T extends object>(target: T, deep?: boolean) {
    return new Proxy(target, {
        get(target, p) {
            const element = target[p as keyof typeof p];
            if (typeof element !== 'function' && (typeof element !== 'object' || element === null)) return element;
            if (deep) return wrap(element);
            return element;
        },
    });
}
