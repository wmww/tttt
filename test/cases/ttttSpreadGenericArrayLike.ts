// @strict: true
// @noEmit: true
// A spread argument of a generic type is variadic when the type itself is array-like (tsc's isArrayLikeType), not when
// its base constraint is: socket.io's `nsp.emit(ev, ...args)` with `args: Parameters<Map[Ev]>`, Ev constrained by a
// conditional type

interface EventsMap {
  [event: string]: any;
}

type EventNames<Map extends EventsMap> = keyof Map & (string | symbol);

type EventNamesWithoutAck<
  Map extends EventsMap,
  K extends EventNames<Map> = EventNames<Map>,
> = IfAny<
  Last<Parameters<Map[K]>> | Map[K],
  K,
  K extends (Parameters<Map[K]> extends never[] ? K : never)
    ? K
    : K extends (
          Last<Parameters<Map[K]>> extends (...args: any[]) => any ? never : K
        )
      ? K
      : never
>;

type EventParams<
  Map extends EventsMap,
  Ev extends EventNames<Map>,
> = Parameters<Map[Ev]>;

type IfAny<T, TypeIfAny = true, TypeIfNotAny = false> =
  IsAny<T> extends true ? TypeIfAny : TypeIfNotAny;

type IsAny<T> = 0 extends 1 & T ? true : false;

type Last<ValueType extends readonly unknown[]> =
  ValueType extends readonly [infer ElementType]
    ? ElementType
    : ValueType extends readonly [infer _, ...infer Tail]
      ? Last<Tail>
      : ValueType extends ReadonlyArray<infer ElementType>
        ? ElementType
        : never;

declare class Emitter<EmitEvents extends EventsMap> {
    emit<Ev extends EventNamesWithoutAck<EmitEvents>>(ev: Ev, ...args: EventParams<EmitEvents, Ev>): boolean;
}

function relay<EmitEvents extends EventsMap, Ev extends EventNamesWithoutAck<EmitEvents>>(
    e: Emitter<EmitEvents>, ev: Ev, ...args: EventParams<EmitEvents, Ev>
) {
    e.emit(ev, ...args);
}
