// `this` in an object literal method that an earlier overload's contextual signature typed (DefaultMethods' `(this: V)`)
// is the settled overload's again, which gives none: the literal (Vue 2's `extend`, @overlastic/vue2)
// @strict: true
interface Vue {}
interface CO<T> {}
type Accessors<T> = { [K in keyof T]: (() => T[K]) | CO<T[K]> };
type DefaultMethods<V> = { [key: string]: (this: V, ...args: any[]) => any };
interface ComponentOptions<V, Methods = DefaultMethods<V>, Computed = {}> {
  computed?: Accessors<Computed>; methods?: Methods;
}
declare function extend<Methods, Computed>(options?: ComponentOptions<Vue, Methods, Computed>): 1;
declare function extend(options?: ComponentOptions<Vue>): 2;
const r = extend({
  computed: { $visible: { get: 1 } },
  methods: { m(type: string) { this.$visible = false; } },
});
