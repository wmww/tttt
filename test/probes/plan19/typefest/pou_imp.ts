import type {PartialOnUndefinedDeep, Merge} from '../../../../thirdparty/corpora/type-fest/index';
declare function expectType<T>(v: T): void;
declare const indexType: {[k: string]: string | undefined; propertyA: string; propertyB: string | undefined};
declare const x: PartialOnUndefinedDeep<typeof indexType>;
expectType<{[k: string]: string | undefined; propertyA: string; propertyB?: string | undefined}>(x);
