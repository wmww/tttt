// origin: npm @sanity/comlink@4.0.3 https://github.com/sanity-io/comlink packages/comlink/tsconfig.bench.json seed 20261216
// flip: yes
// @allowJs: true
// @allowImportingTsExtensions: true
// @checkJs: true
// @erasableSyntaxOnly: true
// @exactOptionalPropertyTypes: false
// @forceConsistentCasingInFileNames: true
// @isolatedModules: true
// @jsx: react-jsx
// @lib: esnext, dom, dom.iterable
// @module: preserve
// @moduleResolution: bundler
// @moduleDetection: force
// @noFallthroughCasesInSwitch: true
// @noImplicitAny: true
// @noImplicitThis: true
// @noImplicitReturns: true
// @noPropertyAccessFromIndexSignature: true
// @noUncheckedIndexedAccess: true
// @noUnusedLocals: false
// @noUnusedParameters: false
// @noImplicitOverride: true
// @noUncheckedSideEffectImports: true
// @resolveJsonModule: true
// @skipLibCheck: true
// @strict: true
// @strictFunctionTypes: true
// @strictNullChecks: true
// @strictPropertyInitialization: true
// @target: esnext
// @verbatimModuleSyntax: true
// @allowSyntheticDefaultImports: true
// @alwaysStrict: true
// @esModuleInterop: true
// @preserveConstEnums: true
// @filename: packages/comlink/node_modules/xstate/dist/declarations/src/createActor.d.ts
import type { AnyActorLogic, AnyActorRef, ConditionalRequired, EmittedFrom, EventFromLogic, InputFrom, IsNotNever, Readable, Snapshot, SnapshotFrom } from "./types.js";
/**
 */
export declare class Actor<TLogic extends AnyActorLogic> implements ActorRef<SnapshotFrom<TLogic>, EventFromLogic<TLogic>, EmittedFrom<TLogic>> {
    select<TSelected>(selector: (snapshot: SnapshotFrom<TLogic>) => TSelected, equalityFn?: (a: TSelected, b: TSelected) => boolean): Readable<TSelected>;
    /**
     */
}
export declare function createActor<TLogic extends AnyActorLogic>(logic: TLogic, ...[options]: ConditionalRequired<[
], IsNotNever<RequiredActorOptionsKeys<TLogic>>>): Actor<TLogic>;
// @filename: packages/comlink/node_modules/xstate/dist/declarations/src/index.d.ts
export { Actor, createActor, interpret, type Interpreter, type RequiredActorOptionsKeys as RequiredActorOptionsKeys } from "./createActor.js";
export { setup, type SetupReturn } from "./setup.js";
export * from "./types.js";
// @filename: packages/comlink/node_modules/xstate/dist/declarations/src/setup.d.ts
export type SetupReturn<TContext extends MachineContext, TEvent extends AnyEventObject, TActors extends Record<string, UnknownActorLogic>, TChildrenMap extends Record<string, string>, TActions extends Record<string, ParameterizedObject['params'] | undefined>, TGuards extends Record<string, ParameterizedObject['params'] | undefined>, TDelay extends string, TTag extends string, TInput, TOutput extends NonReducibleUnknown, TEmitted extends EventObject, TMeta extends MetaObject> = {
    /**
     */
    createMachine: <const TConfig extends MachineConfig<TContext, TEvent, ToProvidedActor<TChildrenMap, TActors>, ToParameterizedObject<TActions>, ToParameterizedObject<TGuards>, TDelay, TTag, TInput, TOutput, TEmitted, TMeta>, TResolvedChildren extends Record<string, string> = MergeChildrenMap<TChildrenMap, Cast<ExtractConfigChildren<TConfig>, Record<string, string>>>>(config: TConfig) => StateMachine<TContext, TEvent | ([RoutableStateId<TConfig>] extends [never] ? never : {
    }), Cast<ToChildren<ToProvidedActor<TResolvedChildren, TActors>>, Record<string, AnyActorRef | undefined>>, ToProvidedActor<TResolvedChildren, TActors>, ToParameterizedObject<TActions>, ToParameterizedObject<TGuards>, TDelay, ToStateValue<TConfig>, TTag, TInput, TOutput, TEmitted, TMeta, ToStateSchema<TConfig>>;
};
export declare function setup<TContext extends MachineContext, TEvent extends AnyEventObject, // TODO: consider using a stricter `EventObject` here
TActors extends Record<string, UnknownActorLogic> = {}, TChildrenMap extends Record<string, string> = {}, TActions extends Record<string, ParameterizedObject['params'] | undefined> = {}, TGuards extends Record<string, ParameterizedObject['params'] | undefined> = {}, TDelay extends string = never, TTag extends string = string, TInput = NonReducibleUnknown, TOutput extends NonReducibleUnknown = NonReducibleUnknown, TEmitted extends EventObject = EventObject, TMeta extends MetaObject = MetaObject>({ schemas, actors, actions, guards, delays }: {
}): SetupReturn<TContext, TEvent, TActors, TChildrenMap, TActions, TGuards, TDelay, TTag, TInput, TOutput, TEmitted, TMeta>;
// @filename: packages/comlink/node_modules/xstate/dist/declarations/src/types.d.ts
import type { Actor } from "./createActor.js";
/**
 */
type ReturnTypeOrValue<T> = T extends AnyFunction ? ReturnType<T> : T;
export interface TransitionDefinition<TContext extends MachineContext, TEvent extends EventObject> extends Omit<TransitionConfig<TContext, TEvent, TEvent, TODO, TODO, TODO, TODO, TODO, // TEmitted
TODO>, 'target' | 'guard'> {
}
export interface ActorRef<TSnapshot extends Snapshot<unknown>, TEvent extends EventObject, TEmitted extends EventObject = EventObject> extends Subscribable<TSnapshot>, InteropObservable<TSnapshot> {
    select<TSelected>(selector: (snapshot: TSnapshot) => TSelected, equalityFn?: (a: TSelected, b: TSelected) => boolean): Readable<TSelected>;
}
export type ActorRefFrom<T> = ReturnTypeOrValue<T> extends infer R ? R extends StateMachine<infer TContext, infer TEvent, infer TChildren, infer _TActor, infer _TAction, infer _TGuard, infer _TDelay, infer TStateValue, infer TTag, infer _TInput, infer TOutput, infer TEmitted, infer TMeta, infer TStateSchema> ? ActorRef<MachineSnapshot<TContext, TEvent, TChildren, TStateValue, TTag, TOutput, TMeta, TStateSchema>, TEvent, TEmitted> : R extends Promise<infer U> ? ActorRefFrom<PromiseActorLogic<U>> : R extends ActorLogic<infer TSnapshot, infer TEvent, infer _TInput, infer _TSystem, infer TEmitted> ? ActorRef<TSnapshot, TEvent, TEmitted> : never : never;
export type AnyActorScope = ActorScope<any, // TSnapshot
AnyActorSystem, any>;
/**
 */
export type SnapshotFrom<T> = ReturnTypeOrValue<T> extends infer R ? R extends ActorRef<infer TSnapshot, infer _, infer __> ? TSnapshot : R extends Actor<infer TLogic> ? SnapshotFrom<TLogic> : R extends ActorLogic<infer _TSnapshot, infer _TEvent, infer _TInput, infer _TEmitted, infer _TSystem> ? ReturnType<R['transition']> : R extends ActorScope<infer TSnapshot, infer _TEvent, infer _TEmitted, infer _TSystem> ? TSnapshot : never : never;
// @filename: packages/comlink/node_modules/xstate/dist/xstate.cjs.d.mts
export * from "./declarations/src/index.js";
// @filename: packages/comlink/src/node.ts
import {
  createActor,
  setup,
  type ActorRefFrom,
} from 'xstate'
import {createRequestMachine, type RequestActorRef} from './request'
/**
 */
export type NodeActorLogic<TSends extends Message, TReceives extends Message> = ReturnType<
  typeof createNodeMachine<TSends, TReceives>
>
/**
 */
export type NodeActor<TSends extends Message, TReceives extends Message> = ActorRefFrom<
  NodeActorLogic<TSends, TReceives>
>
/**
 */
export type Node<TSends extends Message, TReceives extends Message> = {
  actor: NodeActor<TSends, TReceives>
  fetch: <TType extends TSends['type'], TMessage extends Extract<TSends, {type: TType}>>(
    ...params:
      | [TType, TMessage['data'], {signal?: AbortSignal; suppressWarnings?: boolean}]
  ) => TSends extends TMessage
    ? TSends['type'] extends TType
      ? Promise<TSends['response']>
      : never
    : never
}
/**
 */
export const createNodeMachine = <
  TReceives extends Message, // Receives
  TSendsWithoutResponse extends WithoutResponse<TSends> = WithoutResponse<TSends>,
>() => {
  const nodeMachine = setup({
    actors: {
      requestMachine: createRequestMachine<TSends>(),
    },
  }).createMachine({
  })
  return nodeMachine
}
export const createNode = <TSends extends Message, TReceives extends Message>(
  machine: NodeActorLogic<TSends, TReceives> = createNodeMachine<TSends, TReceives>(),
): Node<TSends, TReceives> => {
  const actor = createActor(machine, {
  })
  return {
    actor,
  }
}
// @filename: packages/comlink/src/request.ts
import {
  setup,
} from 'xstate'
const throwOnEvent =
      take(1),
/**
 */
export const createRequestMachine = <TSends extends Message>() => {
  return setup({
    actions: {
      'send message': ({context}, params: {message: ProtocolMessage}) => {
      },
      'on abort': sendTo(
      ),
    },
    guards: {
      expectsResponse: ({context}) => context.expectResponse,
    },
  }).createMachine({
        entry: 'on success',
  })
}
