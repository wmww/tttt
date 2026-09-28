// @strict: true
type Procedure = (...args: any[]) => any;
type Methods<T> = keyof { [K in keyof T as T[K] extends Procedure ? K : never]: T[K] };
type Classes<T> = { [K in keyof T]: T[K] extends new (...args: any[]) => any ? K : never }[keyof T] & (string | symbol);
interface O {
    a: string;
    safeParse(x: unknown): number;
    register<R extends { m: unknown }>(registry: R, ...meta: R["m"] extends string ? [string] : [number]): this;
}
declare function spyOn<T extends object, M extends Classes<Required<T>> | Methods<Required<T>>>(object: T, key: M): M;
declare const o: O;
const r = spyOn(o, "safeParse");
type C = Classes<Required<O>>;
type M = Methods<Required<O>>;
