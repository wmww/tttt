// tsc: no errors (Sum<1, 1> is 2)
import type {Sum} from '../../../thirdparty/corpora/type-fest/index';
declare function expectType<T>(value: T): void;
declare const s1: Sum<1, 1>;
declare const s2: Sum<2, 1>;
const x: 2 = s1;
const y: 3 = s2;
