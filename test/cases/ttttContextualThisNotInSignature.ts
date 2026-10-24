// An object literal method checked first without a contextual signature (Vue 2's extend: Methods, a type parameter)
// has no `this` parameter for good (tsc's assignNonContextualParameterTypes): a later candidate's `(this: V)` reaches
// its body's `this`, not its signature, so the inferred Methods doesn't carry `this: Vue` into ThisType (@overlastic/vue2)
// @strict: true
// @skipLibCheck: true
// @filename: vue.d.ts
export interface Vue {}
type Accessors<T> = { [K in keyof T]: (() => T[K]) | ComputedOptions<T[K]> };
type ThisTypedComponentOptionsWithRecordProps<V extends Vue, Data, Methods, Computed, Props> =
  ComponentOptions<V, DataDef<Data, Props, V>, Methods, Computed, RecordPropsDefinition<Props>, Props> &
  ThisType<CombinedVueInstance<V, Data, Methods, Computed, Readonly<Props>>>;
type DefaultMethods<V> = { [key: string]: (this: V, ...args: any[]) => any };
interface ComponentOptions<V extends Vue, Data = DefaultData<V>, Methods = DefaultMethods<V>, Computed = DefaultComputed, PropsDef = PropsDefinition<DefaultProps>, Props = DefaultProps> {
  props?: PropsDef; computed?: Accessors<Computed>; methods?: Methods;
}
type RecordPropsDefinition<T> = { [K in keyof T]: PropValidator<T[K]> };
interface ComputedOptions<T> { get?(): T; }
type CombinedVueInstance<Instance extends Vue, Data, Methods, Computed, Props> = Data & Methods & Computed & Props & Instance;
interface VueConstructor<V extends Vue = Vue> {
  extend<Data, Methods, Computed, Props>(options?: ThisTypedComponentOptionsWithRecordProps<V, Data, Methods, Computed, Props>): ExtendedVue<V, Data, Methods, Computed, Props>;
  extend(options?: ComponentOptions<V>): ExtendedVue<V, {}, {}, {}, {}>;
}
export declare const Vue: VueConstructor;
// @filename: a.ts
import { Vue } from "./vue";
declare const model: string;
Vue.extend({
  props: { [model]: Boolean },
  computed: { $visible: { get() { return (this as any)[model]; } } },
  methods: { m(type: string) { this.$visible = false; } },
});
