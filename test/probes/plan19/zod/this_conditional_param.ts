interface Reg<M = unknown, S = unknown> { _meta: M; _schema: S; }
interface O {
    a: string;
    register<R extends Reg>(registry: R, ...meta: this extends R["_schema"] ? [R["_meta"]] : ["no"]): this;
}
type Req = Required<O>;
declare const x: Req["register"] extends new (...args: any[]) => any ? 1 : 0;
declare const y: O["register"] extends new (...args: any[]) => any ? 1 : 0;
type Classes<T> = { [K in keyof T]: T[K] extends new (...args: any[]) => any ? K : never }[keyof T] & (string | symbol);
declare const c: Classes<Req>;
