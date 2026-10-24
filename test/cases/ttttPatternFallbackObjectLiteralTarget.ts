// @strict: true
// A destructured parameter whose contextual type inference left `unknown` takes its pattern's type, which tsc keeps
// as an unwidened object literal type: as a relation's target it has no room for other properties. The first overload
// tried assigns it for good (foldkit's kanban: effect's Array.findFirst under pipe)
interface Option<A> { _A: A }
declare function pipe<A, B = never>(a: A, ab: (a: A) => B): B;
declare const findFirst: {
    <A, B>(f: (a: NoInfer<A>, i: number) => Option<B>): (self: Iterable<A>) => Option<B>;
    <A>(predicate: (a: NoInfer<A>, i: number) => boolean): (self: Iterable<A>) => Option<A>;
};
type Card = { readonly id: string; readonly sortKey: string };
declare const draggedId: string;
declare const cards: any;
const viaPipe = (): Option<Card> => pipe(cards, findFirst(({ id }) => id === draggedId));
const direct: (self: any) => Option<Card> = findFirst(({ id }) => id === draggedId);
const known: (self: Iterable<Card>) => Option<Card> = findFirst(({ id }) => id === draggedId);

// Without a contextual type the same pattern's type is widened: no such check
const loose = ({ id }) => id === draggedId;
const card: Card = { id: "", sortKey: "" };
loose(card);
declare function takes(f: (a: Card) => boolean): void;
takes(loose);
