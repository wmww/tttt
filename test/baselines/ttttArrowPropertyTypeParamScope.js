//// [test/cases/ttttArrowPropertyTypeParamScope.ts] ////

//// [ttttArrowPropertyTypeParamScope.ts]
// An arrow function property printed from its expression (the pseudo type) is a scope of its own: the type parameter
// names it gives are free again for its siblings (no b<A_1>)
export const y1 = { a: <A,>(a: A) => {}, b<A>(x: A) {} };
export const y2 = { b<A>(x: A) {}, a: <A,>(a: A) => {} };
export const y3 = { b<A>(x: A) {}, c<A>(x: A) {} };
export const y4 = { a: <A,>(a: A) => {}, c: <A,>(a: A) => {} };
export const y5 = { a: <A,>(a: A) => {}, b<const A>(x: A) {} };
export const y6 = { a: { q: <A,>(a: A) => {} }, b: { r<A>(x: A) {} } };
declare function wrap<T>(f: () => T): { value: T };
export const x = wrap(function () {
  const out = {
    ast: { equals: <A, I>(a: A, b: I) => {} },
    make: { succeed<const A, const B>(input: A, b: B) {} },
  };
  return out;
});
export const z = {
    ast: { equals: <A, I>(a: A, b: I) => {} },
    make: { succeed<const A, const B>(input: A, b: B) {} },
};


//// [ttttArrowPropertyTypeParamScope.js]
// An arrow function property printed from its expression (the pseudo type) is a scope of its own: the type parameter
// names it gives are free again for its siblings (no b<A_1>)
export const y1 = { a: (a) => { }, b(x) { } };
export const y2 = { b(x) { }, a: (a) => { } };
export const y3 = { b(x) { }, c(x) { } };
export const y4 = { a: (a) => { }, c: (a) => { } };
export const y5 = { a: (a) => { }, b(x) { } };
export const y6 = { a: { q: (a) => { } }, b: { r(x) { } } };
export const x = wrap(function () {
    const out = {
        ast: { equals: (a, b) => { } },
        make: { succeed(input, b) { } },
    };
    return out;
});
export const z = {
    ast: { equals: (a, b) => { } },
    make: { succeed(input, b) { } },
};


//// [ttttArrowPropertyTypeParamScope.d.ts]
export declare const y1: {
    a: <A>(a: A) => void;
    b<A>(x: A): void;
};
export declare const y2: {
    b<A>(x: A): void;
    a: <A>(a: A) => void;
};
export declare const y3: {
    b<A>(x: A): void;
    c<A>(x: A): void;
};
export declare const y4: {
    a: <A>(a: A) => void;
    c: <A>(a: A) => void;
};
export declare const y5: {
    a: <A>(a: A) => void;
    b<const A>(x: A): void;
};
export declare const y6: {
    a: {
        q: <A>(a: A) => void;
    };
    b: {
        r<A>(x: A): void;
    };
};
export declare const x: {
    value: {
        ast: {
            equals: <A, I>(a: A, b: I) => void;
        };
        make: {
            succeed<const A, const B>(input: A, b: B): void;
        };
    };
};
export declare const z: {
    ast: {
        equals: <A, I>(a: A, b: I) => void;
    };
    make: {
        succeed<const A, const B>(input: A, b: B): void;
    };
};
