// @strict: true
// @exactOptionalPropertyTypes: true
// @target: esnext
// @module: commonjs
// @noEmit: true
// Reduced from xstate's input.test.ts: a `typeof child` (a function local) inside a type literal made the literal's
// free type parameters unknown, so each inference mapper re-instantiated it and a structural comparison between two
// copies reported a spurious TS2322 under exactOptionalPropertyTypes. The unresolved names are leftovers of the reduction.
// @filename: src/types.ts
import type { StateMachine } from './StateMachine';
import { Spawner } from './spawn';
export type Compute<A> = { [K in keyof A]: A[K] } & unknown;
export type InputFrom<T> =
  T extends StateMachine<
    infer _TContext,
    infer _TEvent,
    infer _TChildren,
    infer _TActor,
    infer _TAction,
    infer _TGuard,
    infer _TDelay,
    infer _TStateValue,
    infer _TTag,
    infer TInput,
    infer _TTransitionMeta
  >
    ? TInput
    : T extends ActorLogic<
          infer _TEmitted
        >
      ? TInput
      : never;

export type ConditionalRequired<
  T,
  Condition extends boolean
> = Condition extends true ? Required<T> : T;

export type SingleOrArray<T> = readonly T[] | T;

type DistributeActors<
  TContext extends MachineContext,
  TEvent extends EventObject,
  TActor extends ProvidedActor,
  TAction extends ParameterizedObject,
  TGuard extends ParameterizedObject,
  TDelay extends string,
  TEmitted extends EventObject,
  TTransitionMeta extends MetaObject,
  TSpecificActor extends ProvidedActor
> = TSpecificActor extends { src: infer TSrc }
  ?
      | Compute<
          {
            input?:
              | Mapper<
                  TContext,
                  TEvent,
                  InputFrom<TSpecificActor['logic']>,
                  TEvent
                >
            onError?:
              | SingleOrArray<
                  TransitionConfigOrTarget<
                    TTransitionMeta
                  >
                >;

            onSnapshot?:
              | SingleOrArray<
                  TransitionConfigOrTarget<
                    TTransitionMeta
                  >
                >;
          } & { [K in RequiredActorOptions<TSpecificActor>]: unknown }
        >
      | {
          input?:
            | SingleOrArray<
                TransitionConfigOrTarget<
                  TTransitionMeta
                >
              >;

          onSnapshot?:
            | SingleOrArray<
                TransitionConfigOrTarget<
                  TTransitionMeta
                >
              >;
        }
  : never;

export type InvokeConfig<
  TContext extends MachineContext,
  TEvent extends EventObject,
  TActor extends ProvidedActor,
  TAction extends ParameterizedObject,
  TGuard extends ParameterizedObject,
  TDelay extends string,
  TEmitted extends EventObject,
  TTransitionMeta extends MetaObject
> =
  IsLiteralString<TActor['src']> extends true
    ? DistributeActors<
        TContext,
        TEvent,
        TActor,
        TAction,
        TGuard,
        TDelay,
        TEmitted,
        TTransitionMeta,
        TActor
      >
    : {
      };

export type AnyInvokeConfig = InvokeConfig<
  any // TTransitionMeta
>;

export interface StateNodeConfig<
  TTransitionMeta extends MetaObject = TStateMeta
> {
  invoke?: SingleOrArray<
    InvokeConfig<
      TContext,
      TEvent,
      TActor,
      TAction,
      TGuard,
      TDelay,
      TEmitted,
      TTransitionMeta
    >
  >;
  route?: RouteTransitionConfig<
    TTransitionMeta
  >;
}

export type AnyStateNodeConfig = StateNodeConfig<
  any // transition meta
>;

export interface StateNodeDefinition<
> {
  invoke: Array<
    InvokeDefinition<
      TTransitionMeta
    >
  >;
}

type InitialContext<
  TContext extends MachineContext,
  TActor extends ProvidedActor,
  TInput,
  TEvent extends EventObject
> = ({
}: {
  spawn: Spawner<TActor>;
  input: TInput;
  self: ActorRef<
    MachineSnapshot<
      TODO // State schema
    >,
    AnyEventObject
  >;
}) => TContext;

export type MachineConfig<
  TContext extends MachineContext,
  TEvent extends EventObject,
  TActor extends ProvidedActor = ProvidedActor,
  TAction extends ParameterizedObject = ParameterizedObject,
  TGuard extends ParameterizedObject = ParameterizedObject,
  TDelay extends string = string,
  TTag extends string = string,
  TInput = any,
  TTransitionMeta extends MetaObject = TStateMeta
> = (Omit<
  StateNodeConfig<
    DoNotInfer<TTransitionMeta>
  >,
  'output'
> & {
}) &
  (MachineContext extends TContext
    ? { context?: InitialContext<LowInfer<TContext>, TActor, TInput, TEvent> }
    : { context: InitialContext<LowInfer<TContext>, TActor, TInput, TEvent> });

export interface MachineTypes<
  TContext extends MachineContext,
  TEvent extends EventObject,
  TActor extends ProvidedActor,
  TTransitionMeta extends MetaObject = TStateMeta
> {
  actors?: TActor;
}

export interface RaiseActionOptions<
> {
}

export type Mapper<
  TContext extends MachineContext,
  TExpressionEvent extends EventObject,
  TResult,
  TEvent extends EventObject
> = (args: {
  context: TContext;
  self: ActorRef<
    AnyEventObject
  >;
}) => TResult;

export interface TransitionDefinition<
> {
  select<TSelected>(
  ): Readable<TSelected>;
}

export type AnyActorRef = ActorRef<
  EmittedFrom<T>
>;

export type InterpreterFrom<
> =
  ReturnTypeOrValue<T> extends StateMachine<
    infer _TTransitionMeta
  >
    ? InternalMachineImplementations<
        ResolvedStateMachineTypes<
          TEmitted
        >
      >
    : never;

export type RequiredActorOptions<TActor extends ProvidedActor> =
  | (undefined extends InputFrom<TActor['logic']> ? never : 'input');

export interface ResolvedStateMachineTypes<
> {
}

export type GetConcreteByKey<
> = T & Record<TKey, TValue>;

// @filename: src/StateMachine.ts
export class StateMachine<
  TAction extends ParameterizedObject,
  TGuard extends ParameterizedObject,
  TDelay extends string,
  TStateValue extends StateValue,
  TTag extends string,
  TInput,
  TOutput,
  TEmitted extends EventObject,
  TStateMeta extends MetaObject,
  TStateSchema extends StateSchema,
  TTransitionMeta extends MetaObject = TStateMeta
> {
  public restoreSnapshot(
    _actorScope: ActorScope<
      MachineSnapshot<
        TStateSchema
      >,
      TEmitted
    >
  ): MachineSnapshot<
    TStateSchema
  > {
    const snapshotChildren: Record<
      {
      }
    > = (snapshot as any).children;

  }
}

// @filename: src/createMachine.ts
import { StateMachine } from './StateMachine';
import {
  MachineConfig,
  MachineTypes,
} from './types';

export function createMachine<
  TContext extends MachineContext,
  TActor extends ProvidedActor,
  TInput,
>(
  config: {
    types?: MachineTypes<
      TContext,
      TEvent,
      TActor,
      TTransitionMeta
    >;
  } & MachineConfig<
    TContext,
    TEvent,
    TActor,
    TAction,
    TGuard,
    TDelay,
    TTag,
    TInput,
    TTransitionMeta
  >,
  implementations?: InternalMachineImplementations<
    ResolvedStateMachineTypes<
      TEmitted
    >
  >
): StateMachine<
  TContext,
  TEvent,
  Cast<ToChildren<TActor>, Record<string, AnyActorRef | undefined>>,
  TActor,
  TAction,
  TGuard,
  TDelay,
  StateValue,
  TTag & string,
  TInput,
  TTransitionMeta
> {
}

// @filename: src/spawn.ts
import {
  ConditionalRequired,
  InputFrom,
  RequiredActorOptions,
} from './types';
type SpawnOptions<
  TActor extends ProvidedActor,
  TSrc extends TActor['src']
> = TActor extends {
}
  ? ConditionalRequired<
      [
        options?: {
          input?: InputFrom<TActor['logic']>;
        } & { [K in RequiredActorOptions<TActor>]: unknown }
      ],
      IsNotNever<RequiredActorOptions<TActor>>
    >
  : never;

export type Spawner<TActor extends ProvidedActor> =
  IsLiteralString<TActor['src']> extends true
    ? {
        <TSrc extends TActor['src']>(
          ...[options]: SpawnOptions<TActor, TSrc>
        ): ActorRefFromLogic<GetConcreteByKey<TActor, 'src', TSrc>['logic']>;
        <TLogic extends AnyActorLogic>(
          ...[options]: ConditionalRequired<
            [
              options?: {
              } & { [K in RequiredLogicInput<TLogic>]: unknown }
            ],
            IsNotNever<RequiredLogicInput<TLogic>>
          >
        ): ActorRefFromLogic<TLogic>;
      }
    : <TLogic extends AnyActorLogic | string>(
        src: TLogic,
        ...[options]: ConditionalRequired<
          [
            options?: {
            } & (TLogic extends AnyActorLogic
              ? { [K in RequiredLogicInput<TLogic>]: unknown }
              : {})
          ],
          IsNotNever<
            TLogic extends AnyActorLogic ? RequiredLogicInput<TLogic> : never
          >
        >
      ) => TLogic extends AnyActorLogic
        ? ActorRefFromLogic<TLogic>
        : AnyActorRef;

// @filename: t.ts
import { createMachine } from './src/createMachine';
function f() {
    const child = createMachine({
      context: ({ input }: { input: number }) => {
      }
    });

    const machine = createMachine(
      {
        types: {} as {
          actors: {
            logic: typeof child;
          };
          context: {
          };
        },
        context: ({ input }) => ({
        }),
        invoke: {
          input: ({ context }) => {
          }
        }
      },
      {
        actors: {
        }
      }
    );
}

