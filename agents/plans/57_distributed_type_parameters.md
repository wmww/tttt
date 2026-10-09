# Plan 57: distributed type parameters (TS 7.1)

TS 7.1 (TypeScript#63708) gives a type parameter referenced inside a distributive conditional type over it a distinct
"distributed" twin. tsgo: `getTypeFromTypeReference` -> `getDistributedTypeParameter(node, t)`: walking up from the
reference node (stopping at a statement), if some enclosing ConditionalTypeNode's check type is a plain identifier
reference to the same type parameter symbol, the reference resolves to `getDistributedTypeFromTypeParameter(t)`: a new
TypeParameter with the same symbol, `isDistributed = true`, constraint = the original T (cached on T as
`distributedType`). `getNonDistributedTypeParameter` maps it back and is used by the mappers (`mapper.go`: every
Map/simple/merged mapper looks the source up non-distributed, so instantiation substitutes both alike), by inference
(inference.go:567-570 for conditional sources, 1533), the node builder (prints as T) and checker.go:28677
(getConstraintOfDistributiveConditionalType), 32131.

Observable effects (tests `distributedTypeParameters`, maybe `distributiveConditionalBaseConstraint`):
- `type Show<A, B extends A>`, `A extends unknown ? B extends unknown ? Show<A, B> : never : never` is now TS2344:
  B's constraint is the outer A, not the distributed A (whose constraint is A: B isn't assignable to it).
- The elaboration: relater.go reportRelationError, case `IsDistributedTypeParameter(target) &&
  isTypeAssignableTo(generalizedSource, target.constraint)`: "'B' is only assignable to the non-distributed 'A', but 'A'
  has been distributed here." (new message X_0_is_only_assignable_to_the_non_distributed_1_but_1_has_been_distributed_here).

tttt: type references lower in `src/ty/lower.c` (TYPE_IDENTIFIER resolving to a type parameter symbol: `ty_type_param`);
there is no node parent pointer, so the lowering needs the enclosing conditional check types in its LowerCtx (push the
check type's symbol when lowering a conditional's extends/true/false branches and its check type itself? tsgo walks
from the node's parent, so the check type node's own reference isn't distributed but anything under the conditional node
is, extends type included). Mappers (`ty_mapper_*` in instantiate.c) must map a distributed param like its original;
printing prints the original's name. Gate cheaply: a lowering flag set only while inside a conditional type node whose
check type is a naked type parameter.

Validate: distributedTypeParameters passes, distributiveConditionalBaseConstraint re-examined, no suite losses, sweep,
expecttypes and corpora flat (type-fest and effect are conditional-heavy: watch their instruction counts).
