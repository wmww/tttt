// origin: npm @overlastic/vue2@0.9.0 https://github.com/hairyf/overlastic tsconfig.bench.json seed 20261106
// flip: no
// @lib: esnext, dom
// @module: esnext
// @moduleResolution: bundler
// @skipLibCheck: true
// @strict: true
// @target: es2018
// @useDefineForClassFields: false
// @filename: packages/@vue2/node_modules/vue/package.json
{"name": "vue", "version": "2.6.0", "main": "dist/vue.runtime.common.js", "module": "dist/vue.runtime.esm.js", "typings": "types/index.d.ts"}
// @filename: packages/@vue2/package.json
{"name": "@overlastic/vue2", "type": "module", "version": "0.9.0", "main": "./src/index.ts"}
// @filename: packages/@vue2/node_modules/vue/types/index.d.ts
import { Vue } from "./vue";
export default Vue;
// @filename: packages/@vue2/node_modules/vue/types/options.d.ts
export type Accessors<T> = {
  [K in keyof T]: (() => T[K]) | ComputedOptions<T[K]>
}
export type ThisTypedComponentOptionsWithArrayProps<V extends Vue, Data, Methods, Computed, PropNames extends string> =
  ComponentOptions<V, DataDef<Data, Record<PropNames, any>, V>, Methods, Computed, PropNames[], Record<PropNames, any>> &
  ThisType<CombinedVueInstance<V, Data, Methods, Computed, Readonly<Record<PropNames, any>>>>;
export type ThisTypedComponentOptionsWithRecordProps<V extends Vue, Data, Methods, Computed, Props> =
  ComponentOptions<V, DataDef<Data, Props, V>, Methods, Computed, RecordPropsDefinition<Props>, Props> &
  ThisType<CombinedVueInstance<V, Data, Methods, Computed, Readonly<Props>>>;
type DefaultMethods<V> =  { [key: string]: (this: V, ...args: any[]) => any };
export interface ComponentOptions<
  V extends Vue,
  Data=DefaultData<V>,
  Methods=DefaultMethods<V>,
  Computed=DefaultComputed,
  PropsDef=PropsDefinition<DefaultProps>,
  Props=DefaultProps> {
  props?: PropsDef;
  computed?: Accessors<Computed>;
  methods?: Methods;
}
export interface FunctionalComponentOptions<Props = DefaultProps, PropDefs = PropsDefinition<Props>> {
}
export interface ComputedOptions<T> {
}
// @filename: packages/@vue2/node_modules/vue/types/vue.d.ts
import {
  ComponentOptions,
  ThisTypedComponentOptionsWithRecordProps,
} from "./options";
export interface Vue {
}
export interface VueConstructor<V extends Vue = Vue> {
  extend<Data, Methods, Computed, Props>(options?: ThisTypedComponentOptionsWithRecordProps<V, Data, Methods, Computed, Props>): ExtendedVue<V, Data, Methods, Computed, Props>;
  extend(options?: ComponentOptions<V>): ExtendedVue<V, {}, {}, {}, {}>;
}
export const Vue: VueConstructor;
// @filename: packages/@vue2/src/composable/define.ts
import Vue from 'vue'
  const mixinOptions = Vue.extend({
    computed: {
      $visible: {
        get() {
        },
      },
    },
    methods: {
      async $runtime_effect(type: 'confirm' | 'cancel', value: any) {
        this.$visible = false
      },
    },
  })

