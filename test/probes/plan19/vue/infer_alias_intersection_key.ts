// @strict: true
// @lib: es2015
interface InjectionConstraint<T> {}
type InjectionKey<T> = symbol & InjectionConstraint<T>
declare function inject<T>(key: InjectionKey<T> | string): T | undefined
declare function inject2<T>(key: InjectionKey<T>): T | undefined
const key: InjectionKey<number> = Symbol()
export const a = inject(key)
export const b = inject2(key)
