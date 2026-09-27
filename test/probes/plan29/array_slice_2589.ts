// tsc: no error. Was TS2589 (Sum<1, 1> was number: generic tuple length resolved eagerly); fixed
import type {ArraySlice} from '../../../thirdparty/corpora/type-fest/index';
declare function expectType<T>(value: T): void;
expectType<ArraySlice<[0, 1, 2, 3], 1>>([1, 2, 3]);
