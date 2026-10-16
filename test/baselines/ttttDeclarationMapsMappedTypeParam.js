//// [test/cases/ttttDeclarationMapsMappedTypeParam.ts] ////

//// [a.ts]
export function f<R>(r: R) {
  type S = Promise<string> & {
    [K in keyof R]: R[K]
  };
  return null as any as S;
}


//// [a.js]
export function f(r) {
    return null;
}


//// [a.d.ts]
export declare function f<R>(r: R): Promise<string> & { [K in keyof R]: R[K]; };
//# sourceMappingURL=a.d.ts.map