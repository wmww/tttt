# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-14 (triage session, todo entry 10 of the fifteenth window): AGENTS.md is a symlink to CLAUDE.md,
the human's "100% green" became plan 66 (plan 56's content mappers un-shelved), the conditional simplification order
dependence went to bugs/type_core.md, the scenarios item was stale (210 / 0 failed). The commit messages have the rest.

Note from human: the disk was full. another agent in another project wiped some cache, but its stillalmost full. you should not need hundreds of gigs of disk, but I think most of the usage is coning from this project. please keep your disk usage reasonable.

From the triage session (2026-10-14, late-bound member merging):
- Late-bound members (ttttLateBoundMemberMerge) left over: an object literal method merged with a late-bound one
  (`{ mm() {}, [m](x: number) {} }`) is typed by the last alone, tsgo's has both signatures (getTypeOfSymbol of the
  merged symbol); `ObjectLiteralProp.methods` is filled but never read, the natural place for it. Several copies of
  "which declarations make one member symbol" remain (class_decl.c's bind_member_symbols/computed_symbol_first, the
  checker's MembersBuilder, check.c's MemberTables, the symbols baseline's computed_member_symbol_items): the symbols
  baseline should ask the checker's Prop decl spans everywhere, as late_member_declarations() and the object
  literal's ty_object_literal_member_symbol() now do.
