// @strict: true
type Data = Record<string, unknown>
type PropConstructor<T = any> = { new (...args: any[]): T & {} } | { (): T }
type PropType<T> = PropConstructor<T> | (PropConstructor<T> | null)[]
interface PropOptions<T = any, D = T> { type?: PropType<T> | true | null; required?: boolean; default?: D | null | undefined | object }
type Prop<T, D = T> = PropOptions<T, D> | PropType<T>
type ComponentObjectPropsOptions<P = Data> = { [K in keyof P]: Prop<P[K]> | null }
type ComponentPropsOptions<P = Data> = ComponentObjectPropsOptions<P> | string[]
type E = { a: number } & { b?: string }
type T1 = E extends ComponentPropsOptions ? 1 : 0
type T2 = E extends ComponentObjectPropsOptions ? 1 : 0
type T3 = E extends ComponentObjectPropsOptions | string[] ? 1 : 0
type T4 = { a: number } extends ComponentPropsOptions ? 1 : 0
type M<O> = { [K in keyof Pick<O, 'a'>]: number } & { [K in keyof Pick<O, never>]?: string }
type E2 = M<{ a: 1 }>
type T5 = E2 extends ComponentPropsOptions ? 1 : 0
type T6 = E2 extends ComponentObjectPropsOptions ? 1 : 0
type T7 = {} extends ComponentObjectPropsOptions ? 1 : 0
type E3 = { a: number } & { [K in never]?: string }
type T8 = E3 extends ComponentPropsOptions ? 1 : 0
