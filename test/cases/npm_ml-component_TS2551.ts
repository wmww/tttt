// @isolatedModules: true
// @jsx: preserve
// @lib: esnext, dom
// @module: esnext
// @moduleResolution: bundler
// @noImplicitAny: false
// @resolveJsonModule: true
// @strict: true
// @strictNullChecks: false
// @target: esnext
// @esModuleInterop: true
// @preserveConstEnums: true
// @filename: node_modules/vue/package.json
{"name": "vue", "version": "2.6.14", "main": "dist/vue.runtime.common.js", "module": "dist/vue.runtime.esm.js", "typings": "types/index.d.ts"}
// @filename: node_modules/element-ui/types/component.d.ts
import Vue from 'vue'
export declare class ElementUIComponent extends Vue {
}
export type ElementUIHorizontalAlignment = 'left' | 'center' | 'right'
// @filename: node_modules/element-ui/types/table-column.d.ts
import { ElementUIComponent, ElementUIHorizontalAlignment } from './component'
export declare class ElTableColumn extends ElementUIComponent {
}
// @filename: node_modules/element-ui/types/table.d.ts
import { ElementUIComponent } from './component'
export declare class ElTable extends ElementUIComponent {
}
// @filename: node_modules/vue/types/index.d.ts
import { Vue } from "./vue";
export default Vue;
export {
  CreateElement,
} from "./vue";
export {
} from "./vnode";
// @filename: node_modules/vue/types/options.d.ts
import { Vue, CreateElement, CombinedVueInstance } from "./vue";
export type Component<Data=DefaultData<never>, Methods=DefaultMethods<never>, Computed=DefaultComputed, Props=DefaultProps> =
  | ComponentOptions<never, Data, Methods, Computed, Props>
type EsModule<T> = T | { default: T }
type ImportedComponent<Data=DefaultData<never>, Methods=DefaultMethods<never>, Computed=DefaultComputed, Props=DefaultProps>
  = EsModule<Component<Data, Methods, Computed, Props>>
export type AsyncComponent<Data=DefaultData<never>, Methods=DefaultMethods<never>, Computed=DefaultComputed, Props=DefaultProps>
  = AsyncComponentPromise<Data, Methods, Computed, Props>
export type AsyncComponentPromise<Data=DefaultData<never>, Methods=DefaultMethods<never>, Computed=DefaultComputed, Props=DefaultProps> = (
) => Promise<ImportedComponent<Data, Methods, Computed, Props>> | void;
export type Accessors<T> = {
}
type DataDef<Data, Props, V> = Data | ((this: Readonly<Props> & V) => Data)
export type ThisTypedComponentOptionsWithArrayProps<V extends Vue, Data, Methods, Computed, PropNames extends string> =
  ThisType<CombinedVueInstance<V, Data, Methods, Computed, Readonly<Record<PropNames, any>>>>;
export type ThisTypedComponentOptionsWithRecordProps<V extends Vue, Data, Methods, Computed, Props> =
  ComponentOptions<V, DataDef<Data, Props, V>, Methods, Computed, RecordPropsDefinition<Props>, Props> &
  ThisType<CombinedVueInstance<V, Data, Methods, Computed, Readonly<Props>>>;
type DefaultData<V> =  object | ((this: V) => object);
type DefaultProps = Record<string, any>;
type DefaultMethods<V> =  { [key: string]: (this: V, ...args: any[]) => any };
type DefaultComputed = { [key: string]: any };
export interface ComponentOptions<
  V extends Vue,
  Data=DefaultData<V>,
  Methods=DefaultMethods<V>,
  Computed=DefaultComputed,
  PropsDef=PropsDefinition<DefaultProps>,
  Props=DefaultProps> {
  data?: Data;
  props?: PropsDef;
  computed?: Accessors<Computed>;
  methods?: Methods;
}
export interface FunctionalComponentOptions<Props = DefaultProps, PropDefs = PropsDefinition<Props>> {
}
export interface RenderContext<Props=DefaultProps> {
}
export type Prop<T> = { (): T } | { new(...args: never[]): T & object } | { new(...args: string[]): Function }
export type PropType<T> = Prop<T> | Prop<T>[];
export type PropValidator<T> = PropOptions<T> | PropType<T>;
export interface PropOptions<T=any> {
  default?: T | null | undefined | (() => T | null | undefined);
}
export type RecordPropsDefinition<T> = {
  [K in keyof T]: PropValidator<T[K]>
}
export type ArrayPropsDefinition<T> = (keyof T)[];
export type PropsDefinition<T> = ArrayPropsDefinition<T> | RecordPropsDefinition<T>;
export interface ComputedOptions<T> {
}
export interface WatchOptions {
}
export interface WatchOptionsWithHandler<T> extends WatchOptions {
}
export type DirectiveFunction = (
) => void;
export interface DirectiveOptions {
}
// @filename: node_modules/vue/types/umd.d.ts
import * as V from "./index";
  export type CreateElement = V.CreateElement;
// @filename: node_modules/vue/types/vnode.d.ts
export type ScopedSlot = (props: any) => ScopedSlotReturnValue;
type ScopedSlotReturnValue = VNode | string | boolean | null | undefined | ScopedSlotReturnArray;
interface ScopedSlotReturnArray extends Array<ScopedSlotReturnValue> {}
export type VNodeChildren = VNodeChildrenArrayContents | [ScopedSlot] | string | boolean | null | undefined;
export interface VNodeChildrenArrayContents extends Array<VNodeChildren | VNode> {}
export interface VNode {
}
export interface VNodeComponentOptions {
}
export interface VNodeData {
}
export interface VNodeDirective {
}
// @filename: node_modules/vue/types/vue.d.ts
import {
  ComponentOptions,
  ThisTypedComponentOptionsWithArrayProps,
  ThisTypedComponentOptionsWithRecordProps,
} from "./options";
export interface CreateElement {
}
export interface Vue {
  readonly $data: Record<string, any>;
}
export type CombinedVueInstance<Instance extends Vue, Data, Methods, Computed, Props> =  Data & Methods & Computed & Props & Instance;
export type ExtendedVue<Instance extends Vue, Data, Methods, Computed, Props> = VueConstructor<CombinedVueInstance<Instance, Data, Methods, Computed, Props> & Vue>;
export interface VueConstructor<V extends Vue = Vue> {
  new <Data = object, Methods = object, Computed = object, PropNames extends string = never>(options?: ThisTypedComponentOptionsWithArrayProps<V, Data, Methods, Computed, PropNames>): CombinedVueInstance<V, Data, Methods, Computed, Record<PropNames, any>>;
  extend<Data, Methods, Computed, Props>(options?: ThisTypedComponentOptionsWithRecordProps<V, Data, Methods, Computed, Props>): ExtendedVue<V, Data, Methods, Computed, Props>;
  extend(options?: ComponentOptions<V>): ExtendedVue<V, {}, {}, {}, {}>;
}
export const Vue: VueConstructor;
// @filename: src/components/BaseInfo/index.tsx
/**
 */
import { MlInfoColumn } from "../../../types/info";
import Vue from "vue";
import { CreateElement } from "vue/types/umd";
export default Vue.extend({
  props: {
    columns: {
      default: (): MlInfoColumn[] => [],
    },
  },
  data() {
    return {
      data_: {},
    };
  },
  computed: {
    columns_() {
      return this.columns.map((column) => {
      });
    },
  },
  methods: {
    getValue(h: CreateElement, column: MlInfoColumn) {
      let value = this.data_?.[column.prop];
    },
  },
});
// @filename: src/shim-global.d.ts
interface AnyObj {
}
// @filename: types/common.d.ts
export interface MlOptions<O = AnyObj> {
  /** 树形的children取值 默认children
   */
}
export interface AnyObj {
}
export type Partial<T> = {
};
// @filename: types/info.d.ts
import { MlOptions } from "./common";
interface MlInfoColumn<D = AnyObj> extends MlOptions {
  prop?: string;
}