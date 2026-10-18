// @strict: true
// Members of a list that print the same name but aren't the same reference (one alias's instantiations: tsc's
// typesAreSameReference compares alias instances) are printed again after the list (tsc's mapToTypeNodes), counted
// twice: past the truncation length sooner, and the reprint's own elisions are what shows
type OptionalPropertyNames<T> = {
    [K in keyof T]-?: {} extends {
        [P in K]: T[K];
    } ? K : never;
}[keyof T];
type SpreadProperties<L, R, K extends keyof L & keyof R> = {
    [P in K]: L[P] | Exclude<R[P], undefined>;
};
type SpreadTwo<L, R> = Pick<L, Exclude<keyof L, keyof R>> & Pick<R, Exclude<keyof R, OptionalPropertyNames<R>>> & Pick<R, Exclude<OptionalPropertyNames<R>, keyof L>> & SpreadProperties<L, R, OptionalPropertyNames<R> & keyof L>;
type Spread<A extends readonly [...any]> = A extends [infer L, ...infer R] ? SpreadTwo<L, Spread<R>> : {};
type ExecutionArgs = { x: number };
declare const v: SpreadTwo<{ req: any; res: any; params: any; }, SpreadTwo<SpreadTwo<{}, {}>, SpreadTwo<unknown, {}>>>;
declare const o: { schema: any; operationName: any; document: any; variableValues: any; contextValue: typeof v; rootValue: { execute: (args: ExecutionArgs) => any; subscribe: (args: ExecutionArgs) => any; } };
const h2: number = o;
async function g() {
  const args = { schema: 1 as any, operationName: 1 as any, document: 1 as any, variableValues: 1 as any, contextValue: v, rootValue: { execute: (args: ExecutionArgs) => 1 as any, subscribe: (args: ExecutionArgs) => 1 as any } };
  return args;
}
const h: (req: string, params: number) => void = (req: any, res: any, params: any) => g();
