//// [test/cases/ttttDeclarationAbstractClassLiteral.ts] ////

//// [ttttDeclarationAbstractClassLiteral.ts]
// A local abstract class written as a type literal: its abstract construct signature alone is a function type (the
// class's prototype isn't written), parenthesized where the context needs it
export function F<T>(x: T) {
  abstract class P { data!: T[]; }
  return P;
}
export function G<T>(x: T) {
  class P { data!: T[]; }
  return P;
}
export function H<T>(x: T) {
  abstract class P { data!: T[]; }
  return [P];
}
export function J<T>(x: T) {
  abstract class P { data!: T[]; static s = 1; }
  return P;
}
export function K<T>(x: T) {
  class P { data!: T[]; }
  return x ? P : undefined;
}
export function L<T>(x: T) {
  abstract class P { data!: T[]; }
  return x ? P : undefined;
}
export function M<T>(x: T) {
  abstract class P { data!: T[]; static s = 1; }
  return x ? P : undefined;
}


//// [ttttDeclarationAbstractClassLiteral.js]
// A local abstract class written as a type literal: its abstract construct signature alone is a function type (the
// class's prototype isn't written), parenthesized where the context needs it
export function F(x) {
    class P {
        data;
    }
    return P;
}
export function G(x) {
    class P {
        data;
    }
    return P;
}
export function H(x) {
    class P {
        data;
    }
    return [P];
}
export function J(x) {
    class P {
        data;
        static s = 1;
    }
    return P;
}
export function K(x) {
    class P {
        data;
    }
    return x ? P : undefined;
}
export function L(x) {
    class P {
        data;
    }
    return x ? P : undefined;
}
export function M(x) {
    class P {
        data;
        static s = 1;
    }
    return x ? P : undefined;
}


//// [ttttDeclarationAbstractClassLiteral.d.ts]
export declare function F<T>(x: T): abstract new () => {
    data: T[];
};
export declare function G<T>(x: T): {
    new (): {
        data: T[];
    };
};
export declare function H<T>(x: T): (abstract new () => {
    data: T[];
})[];
export declare function J<T>(x: T): (abstract new () => {
    data: T[];
}) & {
    s: number;
};
export declare function K<T>(x: T): {
    new (): {
        data: T[];
    };
} | undefined;
export declare function L<T>(x: T): (abstract new () => {
    data: T[];
}) | undefined;
export declare function M<T>(x: T): ((abstract new () => {
    data: T[];
}) & {
    s: number;
}) | undefined;
