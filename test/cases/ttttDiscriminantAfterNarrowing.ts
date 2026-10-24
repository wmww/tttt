// @strict: true
// A name is a discriminant by the declared union while the computed type is a subset of it: the second test of an `||`
// still narrows where the first left a single member
type Ok = { ok: true; value: { v: string }; err?: never };
type Bad = { ok: false; value?: never; err: { message: string } };
declare function get(): Ok | Bad;

export function destructured() {
    const { ok, value, err } = get();
    if (!ok || !value) {
        return err.message;
    }
    return value.v;
}

export function parameters(...[ok, value, err]: [true, { v: string }, undefined] | [false, undefined, Error]) {
    if (!ok || !value) {
        return err.message;
    }
    return value.v;
}

export function aliased() {
    const r = get();
    const { ok, value } = r;
    if (!ok || !value) {
        return r.err.message;
    }
    return r.value.v;
}

export function access() {
    const r = get();
    if (!r.ok || !r.value) {
        return r.err.message;
    }
    return r.value.v;
}
