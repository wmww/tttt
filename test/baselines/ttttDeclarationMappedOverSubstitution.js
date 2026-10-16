//// [test/cases/ttttDeclarationMappedOverSubstitution.ts] ////

//// [a.ts]
export const f = <O, S2 = undefined>() => {
  type X<T> = { [K in keyof T as T[K] extends { a: unknown } ? K : never]: T[K] };
  type Y<T> = { [K in keyof T]?: T[K] };
  type R = O extends { s: infer S } ? S extends { b: string } ? X<S> | Y<S> : undefined : undefined;
  return {} as unknown as undefined extends S2 ? R : S2;
};


//// [a.js]
export const f = () => {
    return {};
};


//// [a.d.ts]
export declare const f: <O, S2 = undefined>() => undefined extends S2 ? O extends {
    s: infer S;
} ? S extends {
    b: string;
} ? { [K in keyof S as S[K] extends {
    a: unknown;
} ? K : never]: S[K]; } | { [K in keyof S]?: S[K] | undefined; } : undefined : undefined : S2;
