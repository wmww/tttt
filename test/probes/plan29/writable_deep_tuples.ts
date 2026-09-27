// tsc: no errors (run it through ./release/tttt with a path relative to this file; filetest.sh can't import).
import type {WritableDeep, ReadonlyDeep} from '../../../thirdparty/corpora/type-fest/index';
declare function expectType<T>(value: T): void;
declare const r: ReadonlyDeep<{a: [string, ...number[]]}>;
declare const t: WritableDeep<typeof r>;
expectType<[string, ...number[]]>(t.a);
declare const r2: ReadonlyDeep<[string, ...number[]]>;
declare const t2: WritableDeep<typeof r2>;
expectType<[string, ...number[]]>(t2);
