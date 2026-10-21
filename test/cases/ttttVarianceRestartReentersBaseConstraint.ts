// @strict: true
// @target: es2022
// @noTypesAndSymbols: true
// (listr2's typings, cut down; angular-cli's tsconfig-test.json) ListrGetRendererOptions' variance is measured while
// the distributive constraint of ListrGetRendererClassFromValue<sub-T> is being computed: its for-constraint tail
// (tsc's getConditionalType loop) relates `typeof SilentRenderer` to `typeof ListrRenderer`, whose constructor's tasks
// restart Task's measurement, outermost again, and in there the same constraint is asked for. tsc moves resolutionStart
// for the outermost variance measurement, so the constraint, in flight below it, is resolved again; taken for a cycle it
// was circular for good and the variance came out covariant rather than bivariant: a spurious TS2322 below
interface ObservableLike<T = any> {
}
interface ReadableLike {
}
declare class Task<Ctx, Renderer extends ListrRendererFactory = ListrRendererFactory, FallbackRenderer extends ListrRendererFactory = ListrRendererFactory> extends ListrTaskEventManager {
  listr: Listr<Ctx, Renderer, FallbackRenderer>;
}
declare class DefaultRenderer implements ListrRenderer {
  static rendererOptions: ListrDefaultRendererOptions;
  create(options?: {
  }): string;
}
interface ListrDefaultRendererOptions extends RendererPresetTimer, RendererLoggerOptions<ListrDefaultRendererLogLevels>, ListrLoggerStyleMap<ListrDefaultRendererLogLevels> {
}
declare class SilentRenderer implements ListrRenderer {
  static rendererOptions: never;
  constructor(tasks: ListrSilentRendererTask[], options: ListrSilentRendererOptions);
}
type ListrSilentRendererTask = ListrRendererTask<typeof SilentRenderer>;
declare class SimpleRenderer implements ListrRenderer {
  static rendererOptions: ListrSimpleRendererOptions;
}
interface ListrSimpleRendererOptions extends RendererPresetTimer, RendererPresetTimestamp, RendererLoggerOptions<ListrLogLevels>, ListrLoggerStyleMap<ListrLogLevels> {
}
declare class TestRenderer implements ListrRenderer {
  static rendererOptions: ListrTestRendererOptions;
}
interface ListrTestRendererOptions extends RendererLoggerOptions<ListrLogLevels> {
}
declare class VerboseRenderer implements ListrRenderer {
  static rendererOptions: ListrVerboseRendererOptions;
}
interface ListrVerboseRendererOptions extends RendererPresetTimer, RendererPresetTimestamp, RendererLoggerOptions<ListrLogLevels>, ListrLoggerStyleMap<ListrLogLevels> {
}
type ListrDefaultRendererValue = 'default';
type ListrDefaultRenderer = typeof DefaultRenderer;
type ListrSimpleRendererValue = 'simple';
type ListrSimpleRenderer = typeof SimpleRenderer;
type ListrVerboseRendererValue = 'verbose';
type ListrVerboseRenderer = typeof VerboseRenderer;
type ListrTestRendererValue = 'test';
type ListrTestRenderer = typeof TestRenderer;
type ListrSilentRendererValue = 'silent';
type ListrSilentRenderer = typeof SilentRenderer;
type ListrRendererValue = ListrSilentRendererValue | ListrDefaultRendererValue | ListrSimpleRendererValue | ListrVerboseRendererValue | ListrTestRendererValue | ListrRendererFactory;
type ListrGetRendererClassFromValue<T extends ListrRendererValue> = T extends ListrDefaultRendererValue ? ListrDefaultRenderer : T extends ListrSimpleRendererValue ? ListrSimpleRenderer : T extends ListrVerboseRendererValue ? ListrVerboseRenderer : T extends ListrTestRendererValue ? ListrTestRenderer : T extends ListrSilentRenderer ? ListrSilentRenderer : T extends ListrRendererFactory ? T : never;
type ListrGetRendererOptions<T extends ListrRendererValue> = T extends ListrRendererValue ? ListrGetRendererClassFromValue<T>['rendererOptions'] : never;
interface ListrSecondaryRendererSelection<T extends ListrRendererValue> extends ListrSecondaryRendererOptions<T> {
}
interface ListrSecondaryRendererOptions<T extends ListrRendererValue> {
  fallbackRendererOptions?: ListrGetRendererOptions<T>;
}
declare class ListrRenderer {
  static rendererOptions: Record<PropertyKey, any>;
  constructor(tasks: Task<any, ListrRendererFactory, ListrRendererFactory>[], options: typeof ListrRenderer.rendererOptions, events?: ListrEventManager);
}
type ListrRendererFactory = typeof ListrRenderer;
interface ListrBaseClassOptions<Ctx = ListrContext, Renderer extends ListrRendererValue = ListrPrimaryRendererValue, FallbackRenderer extends ListrRendererValue = ListrSecondaryRendererValue> extends ListrOptions<Ctx>, ListrPrimaryRendererSelection<Renderer>, ListrSecondaryRendererSelection<FallbackRenderer> {}
interface ListrTask<Ctx = ListrContext, Renderer extends ListrRendererFactory = any, FallbackRenderer extends ListrRendererFactory = any> extends ListrPrimaryRendererTaskOptions<Renderer>, ListrSecondaryRendererTaskOptions<FallbackRenderer> {
  rollback?: ListrTaskFn<Ctx, Renderer, FallbackRenderer>;
}
type ListrTaskResult<Ctx> = string | Promise<any> | Listr<Ctx, ListrRendererValue, ListrRendererValue> | ReadableLike | ObservableLike<any>;
type ListrTaskFn<Ctx, Renderer extends ListrRendererFactory, FallbackRenderer extends ListrRendererFactory> = (ctx: Ctx, task: TaskWrapper<Ctx, Renderer, FallbackRenderer>) => void | ListrTaskResult<Ctx>;
type ListrRendererTask<SelectedRenderer extends ListrRendererFactory> = Task<any, SelectedRenderer, SelectedRenderer>;
declare class Listr<Ctx = ListrContext, Renderer extends ListrRendererValue = ListrPrimaryRendererValue, FallbackRenderer extends ListrRendererValue = ListrSecondaryRendererValue> {
  task: ListrTask<Ctx, ListrGetRendererClassFromValue<Renderer>, ListrGetRendererClassFromValue<FallbackRenderer>> | ListrTask<Ctx, ListrGetRendererClassFromValue<Renderer>, ListrGetRendererClassFromValue<FallbackRenderer>>[];
  options?: ListrBaseClassOptions<Ctx, Renderer, FallbackRenderer>;
}
type ListrContext = any;
interface ListrEventManager {}
interface ListrLoggerStyleMap<T> { s?: T }
type ListrLogLevels = 'a';
interface ListrOptions<C> { c?: C }
interface ListrPrimaryRendererTaskOptions<T> { p?: T }
declare class ListrTaskEventManager {}
interface RendererLoggerOptions<T> { l?: T }
interface RendererPresetTimer {}
interface RendererPresetTimestamp {}
declare class TaskWrapper<C, R, F> { c: C; r: R; f: F }
type ListrDefaultRendererLogLevels = 'b';
interface ListrPrimaryRendererSelection<T> { p?: T }
type ListrPrimaryRendererValue = 'default';
interface ListrSecondaryRendererTaskOptions<T> { s?: T }
type ListrSecondaryRendererValue = 'simple';
type ListrSilentRendererOptions = never;
function h<A extends ListrRendererValue, B extends A>(x: ListrGetRendererOptions<A>): ListrGetRendererOptions<B> { return x; }
