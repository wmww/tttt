import type {PartialOnUndefinedDeep} from '../../../../thirdparty/corpora/type-fest/index';
declare function expectAssignable<T>(v: T): void;
type TestingType = {
	array2: Array<{propertyA: string; propertyB: number | undefined}> | undefined;
};
declare const bar: PartialOnUndefinedDeep<TestingType, {recurseIntoArrays: true}>;
expectAssignable<{
	array2?: Array<{propertyA: string; propertyB?: number | undefined}> | undefined;
}>(bar);
