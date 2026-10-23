# tsgo -> tttt function map

Where a tsgo rule (`thirdparty/typescript/tsc/internal/{checker,binder,parser}/*.go`) lives in tttt's C. Built 2026-10-23 at commit `b5f863aaf` from comments in `src/` that cite the tsgo name, checked against the function bodies; every C function listed was verified to exist at that commit, the Go file is where the name is defined. Rows list the function(s) that do the job, not every mention.

tttt's shape differs from tsgo's: it is lazy per declaration (each `Expr`, `Binding`, `FunctionImpl`, class item has a `LazyType` memo, see documentation.md), files are named after AST node kinds (`expr.c`, `binding.c`, `class_decl.c`, `function_impl.c`, `statement.c`), and `src/ty/` holds the type system (`lower.c` type nodes to `Ty`, `members.c` structured members, `relate.c` relations, `infer.c`, `call.c`, `flow.c` narrowing, `checker.c` type construction, `ty_display.c` printing). Binding is passes over the AST (`*_add_names`, `*_lookup_names`, `binder_link`) rather than one `bind` walk; the parser also does JSDoc application and several grammar checks. Functions named `ty_*` are the cross-file API, the rest are `static`. To refresh: `grep -rn 'tsgoName' src/` for the comments citing it. Rows can go stale; prefer function names over line numbers.


## Parser (parser/parser.go)
| tsgo | tttt | notes |
|---|---|---|
| `parseExpected` (parser.go) | `parser.c` `expect_char`; `parser.c` `expect_closing`; `parser.c` `expected_token_error` | no token objects: the parser works on the text with `hop_*`/`expect_*` helpers |
| `parseOptional` (parser.go) | `parser.c` `hop_char`; `parser.c` `hop_start` |  |
| `parseIdentifier` (parser.go) | `parser.c` `parse_identifier`; `parser.c` `expect_identifier` |  |
| `parseIdentifierName` (parser.go) | `parser.c` `parse_identifier_or_keyword` |  |
| `nextTokenWithoutCheck` (parser.go) | `parser.c` `tok_next`; `parser.c` `hop_token` | lookahead is `Tok` values (`tok_at`, `tok_peek`, `tok_next`), not a scanner state |
| `isListElement` (parser.go) | `parser.c` `list_is_element`; `parser.c` `tok_is_class_member_start`; `parser.c` `tok_is_type_member_start`; `parser.c` `tok_is_heritage_clause_element` | one function plus per-context predicates |
| `abortParsingListOrMoveToNextToken` (parser.go) | `parser.c` `list_abort_or_skip`; `parser.c` `list_report_error` |  |
| `parseBracketedList` (parser.go) | `parser.c` `list_enter`; `parser.c` `list_leave`; `parser.c` `parse_generic_decl` | lists are parsed inline by each caller between `list_enter`/`list_leave` |
| `isStartOfDeclaration` (parser.go) | `parser.c` `tok_is_start_of_declaration`; `parser.c` `starts_declaration` |  |
| `isStartOfType` (parser.go) | `parser.c` `tok_is_start_of_type`; `parser.c` `tok_is_start_of_parameter_type` |  |
| `parseDeclarationWorker` (parser.go) | `parser.c` `parse_decl_statement` | also modifiers and decorators |
| `parseTypeAliasDeclaration` (parser.go) | `parser.c` `parse_type_decl` |  |
| `parseTypeMember` (parser.go) | `parser.c` `parse_class_item_inner`; `parser.c` `parse_index_signature`; `parser.c` `parse_mapped_interface_member` | interface and class members share one parser (`is_type` flag) |
| `parseAccessorDeclaration` (parser.go) | `parser.c` `parse_type_accessor`; `parser.c` `parse_class_item_inner`; `parser.c` `parse_object_item` |  |
| `parseParameters` (parser.go) | `parser.c` `parse_function_args`; `parser.c` `parse_function_args_inner` |  |
| `parseImportDeclarationOrImportEqualsDeclaration` (parser.go) | `parser.c` `parse_import_export_statement`; `parser.c` `parse_import_equals` |  |
| `parseImportOrExportSpecifier` (parser.go) | `parser.c` `parse_import_item_list` |  |
| `parseModuleExportName` (parser.go) | `parser.c` `parse_module_export_name` |  |
| `parsePropertyAccessExpressionRest` (parser.go) | `parser.c` `parse_expr_right_side`; `parser.c` `parse_dot_prop` |  |
| `parseContextualModifier` (parser.go) | `parser.c` `parse_modifier_list_ex`; `parser.c` `tok_is_class_member_modifier` |  |
| `isAwaitExpression` (parser.go) | `parser.c` `await_is_operator` |  |
| `isYieldExpression` (parser.go) | `parser.c` `yield_is_operator_outside_generator`; `parser.c` `yield_await_is_keyword_here` |  |
| `hasParseDiagnostics` (checker.go) | `parser.c` `grammar_error_s`; `parser.c` `parse_file_pass` | grammar errors are dropped in files with parse errors (`SourceFile.has_parse_errors`) instead of testing `hasParseDiagnostics` |
| `parseErrorAtRange` (parser.go) | `parser.c` `parse_error_inner`; `parser.c` `parse_error_s` |  |
| `parseTypeOperatorOrHigher` (parser.go) | `parser.c` `parse_type_inner`; `parser.c` `parse_type_right_side` |  |
| `parseUnaryExpressionOrHigher` (parser.go) | `parser.c` `parse_left_unary_op_expr` |  |
| `parseBinaryExpressionRest` (parser.go) | `parser.c` `parse_expr_right_side`; `parser.c` `parse_binary_op_expr` |  |
| `parseParenthesizedArrowFunctionExpression` (parser.go) | `parser.c` `parse_arrow_function`; `parser.c` `parse_arrow_function_inner`; `parser.c` `arrow_function_tristate` |  |
| `parseObjectLiteralElement` (parser.go) | `parser.c` `parse_object_item` |  |
| `parseJsxElementOrSelfClosingElementOrFragment` (parser.go) | `parser.c` `parse_jsx_element` |  |
| `parseTemplateExpression` (parser.go) | `parser.c` `parse_template` |  |
| `parseSourceFileWorker` (parser.go) | `parser.c` `parse_file`; `parser.c` `parse_file_pass`; `parser.c` `parse_file_chunked` | optionally chunked across threads; function bodies can be skipped (`skip_body`) and parsed on demand (`parse_deferred_body`) |
| `parseJSDocComment` (jsdoc.go) | `parser.c` `parse_jsdoc_comment`; `parser.c` `parse_jsdoc_tag` | JSDoc types are applied to declarations in the parser (`jsdoc_apply_*`) |
| `checkGrammarModifiers` (grammarchecks.go) | `parser.c` `check_modifiers`; `parser.c` `check_js_modifiers` | done at parse time |
| `checkGrammarTypeOperatorNode` (grammarchecks.go) | `parser.c` `parse_type_inner` | done at parse time |
| `grammarErrorOnFirstToken` (grammarchecks.go) | `parser.c` `grammar_error_s` |  |

## Binder (binder/binder.go)
| tsgo | tttt | notes |
|---|---|---|
| `bindSourceFile` (binder.go) | `program.c` `program_declare_files`; `scope.c` `binder_link`; `statement.c` `statement_add_names` | binding is a set of passes: declare (`*_add_names`), resolve (`*_lookup_names`), link aliases (`binder_link`), flow built during the lookup pass |
| `declareSymbol` (binder.go) | `scope.c` `declare_symbol`; `scope.c` `declare_symbol_split`; `scope.c` `scope_init_declaration` | tsc's `*Excludes` masks verbatim |
| `declareModuleMember` (binder.go) | `scope.c` `scope_init_declaration`; `scope.c` `scope_add_port`; `scope.c` `declare_function_export` |  |
| `mergeSymbol` (checker.go) | `scope.c` `merge_symbol` |  |
| `mergeModuleAugmentation` (checker.go) | `scope.c` `merge_augmentations`; `scope.c` `binder_add_augmentation` | augmentations are collected and merged in a post-declare step |
| `collectModuleReferences` (references.go) | `parser.c` `add_module_ref`; `source_file.c` `source_file_add_module_ref` | the parser records each module reference as it parses |
| `bindExportAssignment` (binder.go) | `scope.c` `scope_export_default_expr`; `port_statement.c` `port_statement_add_names` |  |
| `createFlowCondition` (binder.go) | `flow.c` `flow_new_condition` |  |
| `bindExpandoPropertyAssignment` (binder.go) | `scope.c` `add_expando`; `scope.c` `scope_add_expandos`; `scope.c` `add_expando_chain` |  |
| `bindThisPropertyAssignment` (binder.go) | `scope.c` `add_this_property`; `scope.c` `scope_add_this_property` |  |
| `bindExportsOrObjectDefineProperty` (binder.go) | `scope.c` `declare_commonjs_export`; `scope.c` `add_define_property_export` |  |
| `bindModuleExportsAssignment` (binder.go) | `scope.c` `add_commonjs_export`; `scope.c` `scope_add_commonjs_module` |  |
| `createBranchLabel` (binder.go) | `flow.c` `flow_new_branch_label` |  |
| `createLoopLabel` (binder.go) | `flow.c` `flow_new_loop_label` |  |
| `createFlowCall` (binder.go) | `flow.c` `flow_new_call` |  |
| `createFlowSwitchClause` (binder.go) | `flow.c` `flow_new_switch_clause` |  |
| `createFlowMutation` (binder.go) | `flow.c` `flow_assign` |  |
| `addAntecedent` (binder.go) | `flow.c` `flow_add_antecedent` |  |
| `finishFlowLabel` (binder.go) | `flow.c` `flow_finish_label` |  |
| `isNarrowingExpression` (binder.go) | `flow.c` `expr_is_narrowing` |  |
| `isNarrowableReference` (binder.go) | `flow.c` `expr_is_narrowable_reference` |  |
| `isNarrowableOperand` (binder.go) | `flow.c` `is_narrowable_operand` |  |
| `isDottedName (ast)` | `flow.c` `expr_is_dotted_name` |  |
| `bindCondition` (binder.go) | `expr.c` `flow_add_conditions`; `expr.c` `flow_add_nullish_conditions` | flow construction lives in expr.c/statement.c `*_lookup_names` |

## Name resolution, symbols, modules (checker.go)
| tsgo | tttt | notes |
|---|---|---|
| `resolveName (NameResolver.Resolve)` (nameresolver.go) | `scope.c` `scope_resolve`; `scope.c` `scope_get`; `scope.c` `scope_get_value`; `scope.c` `scope_get_type`; `scope.c` `scope_get_namespace` | lookup happens in the resolve-names pass; identifiers keep their `Symbol*` |
| `resolveEntityName` (checker.go) | `several_types.c` `identifier_type_lookup_names`; `several_types.c` `get_symbol_from_namespace`; `scope.c` `resolve_import_alias_path`; `expr.c` `entity_name_symbol` |  |
| `resolveAlias` (checker.go) | `scope.c` `symbol_resolve_alias` |  |
| `tryResolveAlias` (checker.go) | `scope.c` `resolve_alias_target`; `scope.c` `resolve_alias_target_in` |  |
| `getTargetOfAliasDeclaration` (checker.go) | `scope.c` `resolve_alias_target_in`; `scope.c` `resolve_import_alias_path`; `scope.c` `resolve_entity_alias` |  |
| `resolveExternalModule` (checker.go) | `source_file.c` `module_ref_report_resolved`; `source_file.c` `module_ref_report_unresolved`; `program.c` `program_load_module_ref`; `module_resolution.c` `module_resolver_resolve` | split: resolution in module_resolution.c, loading in program.c, diagnostics in source_file.c |
| `resolveESModuleSymbol` (checker.go) | `scope.c` `module_export_equals`; `scope.c` `make_gather_target`; `scope.c` `resolve_module_type_members` |  |
| `canHaveSyntheticDefault` (checker.go) | `scope.c` `can_have_synthetic_default`; `scope.c` `esm_cjs_can_have_synthetic_default` |  |
| `getTypeWithSyntheticDefaultImportType` (checker.go) | `ty/lower.c` `with_synthetic_default`; `ty/lower.c` `dynamic_import_has_synthetic_default` |  |
| `getExportsOfModule` (checker.go) | `scope.c` `module_resolved_exports`; `scope.c` `symbol_collect_exports`; `scope.c` `symbol_get_export` | export * resolved lazily per module |
| `getSpellingSuggestionForName` (checker.go) | `scope.c` `spelling_suggestion`; `scope.c` `spelling_suggestion_ex`; `scope.c` `levenshtein_with_max` |  |
| `getSpellingSuggestionForName (symbols)` (checker.go) | `scope.c` `scope_suggest`; `scope.c` `suggest_in_table`; `scope.c` `suggest_compare_symbols` |  |
| `reportNonexistentProperty` (checker.go) | `ty/check.c` `report_no_property`; `ty/check.c` `ty_report_no_property`; `ty/check.c` `ty_suggested_property` |  |
| `checkAndReportErrorForMissingPrefix / error "cannot find name"` (checker.go) | `scope.c` `scope_report_not_found`; `scope.c` `report_missing_prefix`; `scope.c` `report_misspelled` |  |
| `symbolToString` (printer.go) | `ty/ty_display.c` `out_symbol_name`; `ty/ty_display.c` `out_symbol_name_impl`; `ty/ty_display.c` `out_fully_qualified_name` |  |
| `getAccessibleSymbolChain` (symbolaccessibility.go) | `symbol_chain.c` `accessible_chain`; `symbol_chain.c` `accessible_chain_uncached` | port of tsc's chain logic |
| `getSymbolChain` (nodebuilderimpl.go) | `symbol_chain.c` `symbol_chain`; `symbol_chain.c` `symbol_chain_at` |  |
| `getContainersOfSymbol` (symbolaccessibility.go) | `symbol_chain.c` `containers_of`; `symbol_chain.c` `alternative_modules` |  |
| `getCandidateListForSymbol` (symbolaccessibility.go) | `symbol_chain.c` `candidate_list` |  |
| `needsQualification` (symbolaccessibility.go) | `symbol_chain.c` `needs_qualification` |  |
| `isSymbolAccessible` (emitresolver.go) | `symbol_chain.c` `symbol_accessibility_at`; `symbol_chain.c` `accessibility_compute` |  |
| `isEntityNameVisible` (emitsupport.go) | `symbol_chain.c` `symbol_has_visible_declarations`; `symbol_chain.c` `decl_visible` |  |
| `getNameOfSymbolAsWritten` (nodebuilderimpl.go) | `symbol_chain.c` `written_name` |  |
| `sortByBestName` (nodebuilderimpl.go) | `symbol_chain.c` `sort_by_best_name` |  |

## Types of declarations (checker.go)
| tsgo | tttt | notes |
|---|---|---|
| `getTypeOfVariableOrParameterOrProperty` (checker.go) | `ty/lower.c` `ty_binding_type`; `ty/members.c` `prop_type` | variables go through the declaration `Binding`'s LazyType, properties through `Prop` |
| `getTypeOfSymbol` (checker.go) | `ty/lower.c` `ty_type_of_symbol` | per-symbol, memoised in links; JS expandos via value_entry_type |
| `getTypeOfVariableOrParameterOrPropertyWorker` (checker.go) | `ty/lower.c` `own_type_of_symbol`; `ty/lower.c` `ty_merged_value_type` |  |
| `getDeclaredTypeOfSymbol` (checker.go) | `ty/lower.c` `ty_declared_type_of` |  |
| `getTypeFromTypeNode` (nodebuilderimpl.go) | `ty/lower.c` `ty_from_node`; `ty/lower.c` `lower_node` | lowering of a `TypeNode` to a `Ty` |
| `getTypeFromTypeAliasReference` (checker.go) | `ty/lower.c` `lower_identifier`; `ty/lower.c` `ty_alias_instance` |  |
| `getTypeFromImportTypeNode` (checker.go) | `several_types.c` `import_type_lookup_names`; `ty/lower.c` `lower_identifier` | import types are `IdentifierType`s with an `import_ref` |
| `getTypeFromTypeReference arity checks` (checker.go) | `several_types.c` `identifier_type_check_arity`; `several_types.c` `several_types_check_arity` |  |
| `getTypeOfExpression` (checker.go) | `ty/expr.c` `ty_expr_type`; `ty/flow.c` `ty_flow_expr_type` | `ty_expr_type` goes through the expression's LazyType (cycle-detected) |
| `checkExpression` (checker.go) | `ty/expr.c` `ty_check_expr`; `ty/expr.c` `check_with`; `ty/expr.c` `compute_inner` | `compute_inner` is the big per-tag switch; context passed down as `ExprCtx` |
| `checkExpressionCached` (checker.go) | `ty/expr.c` `ty_expr_type`; `lazy_type.c` `lazy_type_evaluate_inner` | memo is the expression's LazyType slot |
| `getQuickTypeOfExpression` (checker.go) | `ty/flow.c` `ty_quick_expr_type` |  |
| `checkDeclarationInitializer` (checker.go) | `binding.c` `initialized_binding_evaluate_assigned_type`; `binding.c` `initialized_binding_typecheck` |  |
| `checkVariableLikeDeclaration` (checker.go) | `binding.c` `binding_typecheck_at`; `binding.c` `binding_typecheck`; `binding.c` `initialized_binding_typecheck`; `class_decl.c` `class_decl_typecheck_members`; `interface_member.c` `interface_member_typecheck` | one per AST kind (binding, class item, interface member) |
| `getTypeForBindingElement` (checker.go) | `ty/lower.c` `binding_element_of`; `ty/lower.c` `ty_binding_element_type_from` |  |
| `getBindingElementTypeFromParentType` (checker.go) | `ty/lower.c` `binding_element_of`; `ty/lower.c` `binding_assigned_type` |  |
| `getWidenedType` (checker.go) | `ty/members.c` `widen_with_context`; `ty/members.c` `ty_widened_objects`; `ty/members.c` `ty_widened_nullable`; `ty/expr.c` `ty_widened_for_declaration` | widening is split by kind |
| `getWidenedTypeOfObjectLiteral` (checker.go) | `ty/members.c` `widen_object_literal`; `ty/members.c` `widen_with_context` |  |
| `getWidenedTypeForAssignmentDeclaration` (checker.go) | `ty/lower.c` `assignment_symbol_type`; `ty/lower.c` `value_entry_type` |  |
| `getAssignmentDeclarationInitializerType` (checker.go) | `ty/lower.c` `value_entry_type`; `ty/lower.c` `assignment_declaration_type` |  |
| `getUndefinedProperty` (checker.go) | `ty/members.c` `widen_object_literal` |  |
| `padObjectLiteralType` (checker.go) | `ty/lower.c` `ty_pad_initializer_type`; `binding.c` `binding_add_padded_implicit_any_errors` |  |
| `containsSameNamedThisProperty` (checker.go) | `ty/lower.c` `visit_same_this_property`; `ty/lower.c` `reads_same_this_property` |  |
| `getOuterTypeParameters` (checker.go) | `ty/lower.c` `ty_outer_type_params`; `ty/lower.c` `collect_outer_params`; `ty/lower.c` `ty_function_free_params` |  |
| `isTypeParameterPossiblyReferenced` (checker.go) | `ty/lower.c` `collect_free_params`; `ty/lower.c` `typeof_mentions_no_type_params` | tttt computes free type parameters per node instead of a boolean query |
| `getTypeArguments` (checker.go) | `ty/lower.c` `ty_reference_args`; `ty/lower.c` `ty_deferred_reference` | deferred type-reference arguments resolved on demand |
| `pushTypeResolution` (checker.go) | `lazy_type.c` `lazy_type_evaluate_inner`; `ty/members.c` `members_in_flight_push`; `ty/checker.c` `ty_resolving_cycle` | cycles detected by in-flight memo states (LazyType, members claims), not a resolution stack |
| `getTypeOfDottedName` (flow.go) | `ty/flow.c` `type_of_dotted_name`; `ty/flow.c` `is_dotted_name` |  |
| `getInstantiationExpressionType` (checker.go) | `expr.c` `generic_params_expr_typecheck`; `ty/expr.c` `ty_apply_type_arguments` |  |
| `getBaseTypes` (checker.go) | `ty/members.c` `class_base`; `ty/members.c` `builder_add_inherited`; `class_decl.c` `class_heritage_typecheck` | bases are folded into member resolution (`resolve_declared_members`) |
| `getSingleBaseForNonAugmentingSubtype` (checker.go) | `ty/members.c` `ty_non_augmenting_base`; `ty/members.c` `ty_single_base_for_non_augmenting_subtype` |  |
| `getEffectiveConstraintOfIntersection` (relater.go) | `ty/relate.c` `effective_intersection_constraint`; `ty/relate.c` `effective_constraint_of_members` |  |
| `getReturnTypeOfSignature` (checker.go) | `ty/members.c` `signature_return_type`; `ty/function.c` `function_return_type` |  |
| `getReturnTypeFromBody` (checker.go) | `ty/function.c` `infer_return_type`; `ty/function.c` `function_return_type` | return type of a function is inferred lazily from its body |
| `getReturnTypeFromAnnotation` (checker.go) | `function_impl.c` `function_impl_into_type`; `ty/members.c` `signature_declared_never` |  |
| `getSignatureFromDeclaration` (checker.go) | `ty/members.c` `signature_from_function`; `ty/members.c` `function_signature_params` | `Signature` built lazily from a `FunctionImpl`; params in `function_signature_params` |
| `getTypePredicateOfSignature` (relater.go) | `ty/members.c` `signature_predicate`; `ty/flow.c` `ty_infer_type_predicate` |  |
| `addOptionality` (checker.go) | `ty/members.c` `ty_sig_param_adds_undefined`; `ty/members.c` `function_signature_params` |  |
| `getParameterCount` (relater.go) | `ty/relate.c` `ty_signature_param_count` |  |
| `getEffectiveRestType` (relater.go) | `ty/relate.c` `effective_rest_type`; `ty/infer.c` `effective_rest_type` |  |

## Type construction (checker.go)
| tsgo | tttt | notes |
|---|---|---|
| `getUnionType` (checker.go) | `ty/checker.c` `ty_union`; `ty/checker.c` `ty_union_unreduced`; `ty/checker.c` `union_with_origin`; `ty/expr.c` `ty_union_subtype_reduced` | `ty_union_subtype_reduced` is UnionReduction.Subtype; origins (`make_origin`) kept for printing |
| `getIntersectionType` (checker.go) | `ty/checker.c` `ty_intersection`; `ty/checker.c` `intersection`; `ty/checker.c` `intersect_unions` |  |
| `filterType` (checker.go) | `ty/checker.c` `ty_union_filtered`; `ty/flow.c` `filter_type` |  |
| `getReducedType` (checker.go) | `ty/checker.c` `ty_reduced_slow` | asked where tsc asks it, not at construction |
| `getIndexedAccessType` (checker.go) | `ty/checker.c` `ty_indexed`; `ty/checker.c` `ty_indexed_or_null`; `ty/access.c` `ty_access` |  |
| `getPropertyTypeForIndexType` (checker.go) | `ty/access.c` `access_uncached`; `ty/access.c` `ty_access` | `access_uncached` is the property/index-signature/tuple lookup for one index type |
| `getIndexType / keyof` (checker.go) | `ty/checker.c` `ty_keyof`; `ty/checker.c` `keyof_keyof` |  |
| `getIndexTypeForMappedType` (checker.go) | `ty/checker.c` `ty_keyof`; `ty/relate.c` `mapped_keyof_names`; `ty/relate.c` `apparent_mapped_type_keys` |  |
| `getConditionalType` (checker.go) | `ty/checker.c` `ty_conditional`; `ty/conditional.c` `ty_conditional_resolve`; `ty/instantiate.c` `instantiate_conditional` | branches are `TyCondBranch`es lowered lazily |
| `getMappedType / instantiateMappedType` (mapper.go) | `ty/instantiate.c` `instantiate_anonymous`; `ty/instantiate.c` `instantiate_mapped_over_list`; `ty/members.c` `ty_mapped_info_instantiate` | mapped types are anonymous types with a mapped-info |
| `getTemplateLiteralType` (checker.go) | `ty/template.c` `ty_template_literal`; `ty/template.c` `make_template` |  |
| `getStringMappingType` (checker.go) | `ty/template.c` `ty_string_mapping` |  |
| `isTypeMatchedByTemplateLiteralType` (relater.go) | `ty/template.c` `ty_template_matches`; `ty/template.c` `template_matches` |  |
| `createNormalizedTupleType` (checker.go) | `ty/checker.c` `normalized_tuple`; `ty/checker.c` `ty_tuple_target` |  |
| `createTypeReference` (checker.go) | `ty/checker.c` `ty_reference`; `ty/checker.c` `ty_array` |  |
| `getTotalFixedElementCount` (checker.go) | `ty/access.c` `ty_tuple_fixed_length`; `ty/infer.c` `tuple_fixed_length` |  |
| `sliceTupleType` (relater.go) | `ty/infer.c` `ty_slice_tuple`; `ty/lower.c` `ty_tuple_rest_slice` |  |
| `getTupleElementTypeOutOfStartCount` (checker.go) | `ty/access.c` `ty_tuple_slice_element_type` |  |
| `getFreshTypeOfLiteralType` (checker.go) | `ty/checker.c` `ty_fresh_literal` |  |
| `getRegularTypeOfLiteralType` (checker.go) | `ty/checker.c` `ty_regular` |  |
| `getStringLiteralType` (checker.go) | `ty/checker.c` `ty_literal`; `ty/checker.c` `ty_string_literal` |  |
| `getNonNullableTypeIfNeeded` (checker.go) | `ty/checker.c` `ty_non_nullable` |  |
| `getBaseTypeOfLiteralType` (checker.go) | `ty/relate.c` `ty_base_type_of_literal` |  |
| `getWidenedLiteralType` (checker.go) | `ty/relate.c` `ty_widened_literal` |  |
| `getTypeAtPosition` (relater.go) | `ty/relate.c` `ty_signature_param_type_at`; `ty/relate.c` `param_type_at` |  |
| `getNumberLiteralType` (checker.go) | `ty/checker.c` `ty_literal` |  |
| `getRestrictiveTypeParameter` (checker.go) | `ty/conditional.c` `restrictive_type_param` |  |
| `getImpliedConstraint` (checker.go) | `ty/lower.c` `implied_constraint`; `ty/lower.c` `ty_implied_infer_constraint` |  |
| `getTypeFacts` (checker.go) | `ty/flow.c` `type_facts`; `ty/flow.c` `has_type_facts` |  |
| `isGenericObjectType` (checker.go) | `ty/expr.c` `ty_is_generic_type`; `ty/check.c` `ty_is_generic_object` | generic-ness is cached in type flags |
| `isGenericMappedType` (checker.go) | `ty/conditional.c` `is_generic_mapped` |  |
| `isArrayLikeType` (checker.go) | `ty/relate.c` `is_array_like_type`; `ty/function.c` `ty_extends_array` |  |
| `isMutableArrayLikeType` (checker.go) | `ty/relate.c` `ty_is_mutable_array_or_tuple` |  |
| `isTupleLikeType` (checker.go) | `ty/expr.c` `context_is_tuple_like`; `ty/check.c` `is_tuple_like` |  |
| `isObjectLiteralType` (utilities.go) | `ty/members.c` `is_object_literal_type` |  |
| `isNumericLiteralName` (utilities.go) | `ty/relate.c` `ty_is_numeric_name` |  |
| `newTypeMapper / combineTypeMappers` (mapper.go) | `ty/instantiate.c` `ty_mapper_new`; `ty/instantiate.c` `ty_compose_mappers` |  |

## Members, apparent type, index infos (checker.go)
| tsgo | tttt | notes |
|---|---|---|
| `resolveStructuredTypeMembers` (checker.go) | `ty/members.c` `resolve_members`; `ty/members.c` `resolve_members_inner`; `ty/members.c` `ty_members_resolving` | `Members` memoised on the type; in-flight claims for cycles |
| `resolveObjectTypeMembers / declared members` (checker.go) | `ty/members.c` `resolve_declared_members`; `ty/members.c` `early_declared_members` |  |
| `resolveMappedTypeMembers` (checker.go) | `ty/members.c` `expand_mapped`; `ty/members.c` `expand_generic_homomorphic` |  |
| `resolveUnionTypeMembers` (checker.go) | `ty/members.c` `union_members`; `ty/access.c` `union_index_applies` |  |
| `resolveIntersectionTypeMembers` (checker.go) | `ty/members.c` `resolve_intersection_members` |  |
| `resolveTypeReferenceMembers` (checker.go) | `ty/members.c` `resolve_instantiated_members`; `ty/members.c` `resolve_tuple_members` |  |
| `getPropertiesOfType` (checker.go) | `ty/members.c` `resolve_members`; `ty/members.c` `ty_get_prop` | read `Members.props` |
| `getPropertiesOfObjectType` (checker.go) | `ty/members.c` `resolve_members` |  |
| `getPropertiesOfUnionOrIntersectionType` (checker.go) | `ty/members.c` `union_members`; `ty/members.c` `resolve_intersection_members` |  |
| `getPropertyOfType` (checker.go) | `ty/relate.c` `ty_property_of_type`; `ty/relate.c` `ty_find_prop`; `ty/relate.c` `find_prop` |  |
| `getTypeOfPropertyOfType` (checker.go) | `ty/flow.c` `ty_prop_type_of`; `ty/members.c` `prop_type` |  |
| `getTypeOfSymbol (property)` (checker.go) | `ty/members.c` `prop_type`; `ty/members.c` `prop_write_type` | property types are lazy on the `Prop` |
| `createUnionOrIntersectionProperty` (checker.go) | `ty/relate.c` `find_prop_in`; `ty/access.c` `access_uncached` | synthesized per query, not a cached symbol |
| `getNamedMembers` (checker.go) | `ty/members.c` `sort_props_by_name`; `ty/members.c` `builder_finish` |  |
| `getApparentType` (checker.go) | `ty/relate.c` `apparent_type`; `ty/relate.c` `ty_apparent_type`; `ty/relate.c` `apparent_type_inner` |  |
| `getBaseConstraintOfType` (checker.go) | `ty/relate.c` `ty_base_constraint`; `ty/relate.c` `resolved_base_constraint` |  |
| `getResolvedBaseConstraint` (checker.go) | `ty/relate.c` `resolved_base_constraint`; `ty/relate.c` `compute_resolved_base_constraint` | computeBaseConstraint is `compute_base_constraint` |
| `getConstraintOfType` (checker.go) | `ty/relate.c` `constraint_of_type`; `ty/relate.c` `constraint_of_indexed` |  |
| `getDefaultConstraintOfConditionalType` (checker.go) | `ty/relate.c` `ty_conditional_constraint`; `ty/relate.c` `distributive_constraint` |  |
| `getSimplifiedType` (checker.go) | `ty/relate.c` `ty_simplified`; `ty/relate.c` `compute_simplified` |  |
| `getSimplifiedIndexType` (checker.go) | `ty/relate.c` `compute_simplified`; `ty/relate.c` `ty_distribute_index_over_object` |  |
| `getSimplifiedConditionalType` (checker.go) | `ty/relate.c` `compute_simplified_conditional` |  |
| `getIndexInfosOfType` (checker.go) | `ty/members.c` `resolve_members`; `ty/access.c` `ty_applicable_index_info` |  |
| `getApplicableIndexInfo` (checker.go) | `ty/access.c` `ty_applicable_index_info`; `ty/access.c` `find_applicable_index_info` |  |
| `getUnionIndexInfos` (checker.go) | `ty/access.c` `access_index_infos`; `ty/access.c` `union_index_applies` |  |
| `getSignaturesOfType` (checker.go) | `ty/members.c` `resolve_members`; `ty/relate.c` `ty_union_signatures`; `ty/relate.c` `find_matching_signatures` | union signatures combined in `ty_union_signature` |
| `getUnionSignatures` (checker.go) | `ty/relate.c` `ty_union_signatures`; `ty/relate.c` `ty_union_signature` |  |
| `getArrayMemberCallSignatures` (checker.go) | `ty/call.c` `array_fallback_elements`; `ty/call.c` `array_fallback_callee` |  |
| `getSpreadType` (checker.go) | `ty/expr.c` `spread_into`; `ty/expr.c` `spread_alt_type`; `ty/expr.c` `spread_props_of` |  |
| `getSpreadSymbol` (checker.go) | `ty/expr.c` `spread_props_of`; `ty/expr.c` `ty_is_spreadable_prop` |  |
| `isValidSpreadType` (checker.go) | `ty/check.c` `ty_is_valid_spread_type`; `ty/check.c` `ty_check_spread_type` |  |

## Instantiation (checker.go, mapper.go)
| tsgo | tttt | notes |
|---|---|---|
| `instantiateType` (checker.go) | `ty/instantiate.c` `instantiate_inner`; `ty/instantiate.c` `ty_instantiate_tail` | iterative with a `tail` flag; mappers are `TyMapper`s |
| `instantiateTypeWorker` (checker.go) | `ty/instantiate.c` `instantiate_inner`; `ty/instantiate.c` `instantiate_structured` |  |
| `getObjectTypeInstantiation` (checker.go) | `ty/instantiate.c` `instantiate_anonymous`; `ty/instantiate.c` `ty_realias` | per-target instantiation cache keyed by the outer type arguments |
| `instantiateSignature` (checker.go) | `ty/relate.c` `ty_erased_signature`; `ty/infer.c` `ty_instantiate_signature_in_context_of` | signatures are instantiated lazily with the mapper |
| `getInferredType` (inference.go) | `ty/infer.c` `inference_mapper_impl`; `ty/infer.c` `constraint_pass_resolve` | the mapper resolves a type parameter on demand |
| `instantiateSignatureInContextOf` (checker.go) | `ty/infer.c` `ty_instantiate_signature_in_context_of`; `ty/call.c` `ty_generic_function_in_context` |  |

## Relation (relater.go)
| tsgo | tttt | notes |
|---|---|---|
| `isTypeAssignableTo` (relater.go) | `ty/relate.c` `ty_is_related`; `ty/check.c` `ty_check_assignable` |  |
| `checkTypeRelatedTo` (relater.go) | `ty/relate.c` `check_related`; `ty/relate.c` `ty_is_related`; `ty/check.c` `check_assignable` | `check_related` is the entry with error reporting (`VecTyReportLine`) |
| `isRelatedTo` (relater.go) | `ty/relate.c` `is_related_to`; `ty/relate.c` `plain_related` | `RelationState` threads the maybe-stack and depth |
| `recursiveTypeRelatedTo` (relater.go) | `ty/relate.c` `recursive_related`; `ty/relate.c` `relation_cache_get` | memo in a per-checker relation cache (and the shared graph) |
| `structuredTypeRelatedTo` (relater.go) | `ty/relate.c` `structured_related`; `ty/relate.c` `structured_related_impl` |  |
| `structuredTypeRelatedToWorker` (relater.go) | `ty/relate.c` `structured_related_members` |  |
| `getNormalizedType` (checker.go) | `ty/relate.c` `normalized_intersection`; `ty/relate.c` `ty_normalized_generic_tuple` |  |
| `getRecursionIdentityFromTarget / getRecursionIdentityTarget` (relater.go) | `ty/relate.c` `recursion_identity`; `ty/relate.c` `recursion_key` |  |
| `isDeeplyNestedType` (relater.go) | `ty/relate.c` `relation_deeply_nested`; `ty/relate.c` `ty_is_deeply_nested` |  |
| `propertiesRelatedTo` (relater.go) | `ty/relate.c` `props_related`; `ty/relate.c` `props_related_impl` |  |
| `isPropertySymbolTypeRelated` (relater.go) | `ty/relate.c` `props_related_impl`; `ty/relate.c` `related_to_discriminated` |  |
| `signaturesRelatedTo` (relater.go) | `ty/relate.c` `signatures_related` |  |
| `compareSignaturesRelated` (relater.go) | `ty/relate.c` `compare_signatures` |  |
| `indexSignaturesRelatedTo` (relater.go) | `ty/relate.c` `index_signatures_related` |  |
| `typeArgumentsRelatedTo` (relater.go) | `ty/relate.c` `type_args_related` |  |
| `getVariances` (relater.go) | `ty/relate.c` `get_variances`; `ty/relate.c` `measure_variances` | variance measured by marker instantiation; `TTTT_DUMP_VARIANCES=1` |
| `mappedTypeRelatedTo` (relater.go) | `ty/relate.c` `mapped_related`; `ty/relate.c` `generic_mapped_target_related` |  |
| `isExactOptionalPropertyMismatch / getExactOptionalUnassignableProperties` (checker.go) | `ty/relate.c` `ty_exact_optional_mismatch`; `ty/relate.c` `exact_optional_unassignable_in` |  |
| `discriminateTypeByDiscriminableItems` (relater.go) | `ty/relate.c` `related_for_matching`; `ty/relate.c` `discriminated_union_target`; `ty/expr.c` `discriminate_by_items` |  |
| `typeRelatedToDiscriminatedType` (relater.go) | `ty/relate.c` `related_to_discriminated` |  |
| `findMatchingDiscriminantType / best matching` (relater.go) | `ty/relate.c` `best_matching_type`; `ty/relate.c` `ty_best_matching_type` |  |
| `filterPrimitivesIfContainsNonPrimitive` (relater.go) | `ty/relate.c` `discriminated_union_target` |  |
| `isKnownProperty / hasExcessProperties` (relater.go) | `ty/relate.c` `has_excess_properties`; `ty/relate.c` `is_known_property` |  |
| `isWeakType` (relater.go) | `ty/relate.c` `is_weak_type` |  |
| `isEmptyObjectType` (checker.go) | `ty/relate.c` `is_empty_object_type`; `ty/relate.c` `ty_is_empty_object_type` |  |
| `isSimpleTypeRelatedTo` (relater.go) | `ty/relate.c` `is_simple_related` |  |
| `reportRelationError` (relater.go) | `ty/relate.c` `report_relation_error`; `ty/relate.c` `report_relation_error_now` | diagnostics chain built as `TyReportLine`s |
| `isTypeIdenticalTo` (relater.go) | `ty/relate.c` `types_identical`; `ty/relate.c` `identity_related` |  |
| `compareSignaturesIdentical` (relater.go) | `ty/relate.c` `signatures_identical`; `ty/relate.c` `ty_signatures_identical` |  |
| `getCommonSupertype` (inference.go) | `ty/infer.c` `common_supertype`; `ty/infer.c` `single_common_supertype` |  |

## Inference (inference.go)
| tsgo | tttt | notes |
|---|---|---|
| `newInferenceContext` (inference.go) | `ty/infer.c` `inference_init`; `ty/infer.c` `inference_zero` | an `Inference` struct per call or conditional, not a reusable context object |
| `inferTypes` (inference.go) | `ty/infer.c` `infer_types`; `ty/infer.c` `infer_types_from` |  |
| `inferFromTypes` (inference.go) | `ty/infer.c` `infer_from`; `ty/infer.c` `infer_with_priority` |  |
| `inferToMultipleTypes (unions)` (inference.go) | `ty/infer.c` `infer_to_union` |  |
| `inferFromMatchingTypes` (inference.go) | `ty/infer.c` `infer_to_union` |  |
| `inferFromObjectTypes` (inference.go) | `ty/infer.c` `infer_from_objects`; `ty/infer.c` `infer_to_tuple`; `ty/infer.c` `infer_to_mapped` |  |
| `inferFromProperties` (inference.go) | `ty/infer.c` `infer_from_objects` |  |
| `inferFromSignatures` (inference.go) | `ty/infer.c` `infer_from_signatures`; `ty/infer.c` `infer_from_signature_lists` |  |
| `inferFromIndexTypes` (inference.go) | `ty/infer.c` `infer_from_signatures_and_indexes` |  |
| `inferFromTypeArguments` (inference.go) | `ty/infer.c` `infer_from_type_args` |  |
| `inferToTemplateLiteralType` (inference.go) | `ty/infer.c` `infer_to_template`; `ty/template.c` `ty_infer_from_template` |  |
| `inferToMappedType` (inference.go) | `ty/infer.c` `infer_to_mapped`; `ty/infer.c` `reverse_mapped_type` |  |
| `createReverseMappedType` (inference.go) | `ty/infer.c` `reverse_mapped_type`; `ty/infer.c` `reverse_mapped_type_uncached` |  |
| `applyToParameterTypes` (inference.go) | `ty/infer.c` `infer_from_params`; `ty/infer.c` `infer_from_rest_params` |  |
| `applyToReturnTypes` (inference.go) | `ty/infer.c` `infer_from_returns`; `ty/infer.c` `infer_from_params` |  |
| `getContravariantInference` (inference.go) | `ty/infer.c` `contra_intersection`; `ty/infer.c` `common_subtype` |  |
| `getCovariantInference` (inference.go) | `ty/infer.c` `common_supertype`; `ty/infer.c` `single_common_supertype` |  |
| `unionObjectAndArrayLiteralCandidates` (inference.go) | `ty/infer.c` `inference_call_result_fallback` |  |
| `inferTypeArguments` (checker.go) | `ty/call.c` `infer_type_arguments`; `ty/call.c` `infer_from_arg` |  |
| `getInferredType (conditional `infer`)` (inference.go) | `ty/conditional.c` `conditional_infer` |  |

## Calls and overloads (checker.go)
| tsgo | tttt | notes |
|---|---|---|
| `resolveCall` (checker.go) | `ty/call.c` `resolve_call`; `ty/call.c` `resolve_call_inner`; `ty/call.c` `resolve_with_signatures_inner` |  |
| `resolveCallExpression` (checker.go) | `ty/call.c` `resolve_call_inner` |  |
| `chooseOverload` (checker.go) | `ty/call.c` `resolve_with_signatures_inner`; `ty/call.c` `make_candidate`; `ty/call.c` `add_implementation_success` | candidate loop in `resolve_with_signatures_inner` |
| `reorderCandidates` (checker.go) | `ty/call.c` `reorder_candidates` |  |
| `hasCorrectArity` (checker.go) | `ty/call.c` `has_correct_arity` |  |
| `hasCorrectTypeArgumentArity` (checker.go) | `ty/call.c` `has_correct_type_argument_arity`; `ty/call.c` `report_type_argument_arity` |  |
| `isSignatureApplicable` (checker.go) | `ty/call.c` `check_args`; `ty/call.c` `check_arg`; `ty/call.c` `first_unfit_argument` |  |
| `getEffectiveCallArguments` (checker.go) | `ty/call.c` `collect_args`; `ty/expr.c` `effective_call_args` |  |
| `getSpreadArgumentType` (checker.go) | `ty/call.c` `rest_args_tuple`; `ty/call.c` `rest_args_tuple_ctx` |  |
| `resolveUntypedCall` (checker.go) | `ty/call.c` `check_args_untyped` |  |
| `getResolvedSignature` (checker.go) | `ty/call.c` `ty_call_resolved_signature`; `ty/call.c` `ty_call_resolved_candidate` |  |
| `getEffectsSignature` (flow.go) | `ty/call.c` `ty_call_effects`; `ty/call.c` `chosen_effects` |  |
| `hasTypePredicateOrNeverReturnType` (flow.go) | `ty/call.c` `has_predicate_or_declared_never`; `ty/call.c` `union_call_effects` |  |
| `getContextualTypeForArgument` (checker.go) | `ty/call.c` `literal_arg_context`; `ty/call.c` `rest_arg_contextual` |  |
| `resolveDecorator` (checker.go) | `ty/call.c` `ty_check_decorator`; `ty/call.c` `decorator_call` |  |
| `resolveTaggedTemplateExpression` (checker.go) | `ty/call.c` `tagged_template_call`; `ty/call.c` `ty_tagged_template_typecheck` |  |
| `getJsxPropsTypeFromCallSignature / resolveJsxOpeningLikeElement` (jsx.go) | `ty/call.c` `ty_resolve_jsx_element`; `ty/jsx.c` `ty_jsx_signature_props` |  |
| `getAwaitedType` (checker.go) | `ty/call.c` `ty_awaited`; `ty/call.c` `awaited_type` |  |
| `getPromisedTypeOfPromiseEx` (checker.go) | `ty/call.c` `ty_promised_type`; `ty/call.c` `promised_type_of` |  |
| `createPromiseType` (checker.go) | `ty/function.c` `ty_promise_of` |  |
| `getIterationTypesOfIterable` (checker.go) | `ty/iterate.c` `ty_iteration_types_of_iterable`; `ty/iterate.c` `iterable_types_of` |  |
| `getIterationTypesOfIterator` (checker.go) | `ty/iterate.c` `ty_iteration_types_of_iterator` |  |
| `checkIteratedTypeOrElementType` (checker.go) | `ty/iterate.c` `ty_iterated_type`; `ty/iterate.c` `ty_iterated_type_sending` |  |

## Expression checking (checker.go)
| tsgo | tttt | notes |
|---|---|---|
| `checkIdentifier` (checker.go) | `expr.c` `identifier_expr_typecheck`; `ty/expr.c` `identifier_flow_type` | errors in expr.c, type and flow in ty/expr.c |
| `checkPropertyAccessExpression` (checker.go) | `ty/expr.c` `compute_inner`; `ty/check.c` `check_access`; `ty/check.c` `ty_check_property_access`; `expr.c` `expr_property_access_errors` | type in `compute_inner`'s EXPR_PROPERTY case, errors in ty/check.c |
| `checkElementAccessExpression` (checker.go) | `ty/check.c` `check_indexed_access_part`; `ty/expr.c` `access_flow_type` |  |
| `checkIndexedAccessIndexType` (checker.go) | `ty/check.c` `ty_check_indexed_access_type`; `ty/check.c` `check_indexed_access_part`; `ty/check.c` `ty_indexed_access_index_ok` |  |
| `checkNonNullExpression / checkNonNullType` (checker.go) | `ty/check.c` `ty_check_non_null_type`; `ty/check.c` `ty_check_non_null_non_void_at` |  |
| `checkCallExpression` (checker.go) | `ty/call.c` `call_typecheck`; `ty/call.c` `ty_call_typecheck`; `ty/call.c` `candidate_return_type` |  |
| `checkObjectLiteral` (checker.go) | `ty/expr.c` `check_object`; `ty/members.c` `ty_object_literal` |  |
| `checkArrayLiteral` (checker.go) | `ty/expr.c` `check_array`; `ty/expr.c` `ty_array_like_with_literal_length` |  |
| `checkBinaryExpression` (checker.go) | `expr.c` `binary_expr_typecheck`; `expr.c` `binary_op_typecheck`; `ty/expr.c` `check_binary` |  |
| `checkAssignmentOperator` (checker.go) | `expr.c` `binary_op_typecheck`; `ty/check.c` `ty_check_assignable_expr` |  |
| `checkPrefixUnaryExpression` (checker.go) | `ty/expr.c` `check_unary`; `ty/expr.c` `unary_result_type` |  |
| `checkTemplateExpression` (checker.go) | `ty/expr.c` `check_template` |  |
| `checkYieldExpression` (checker.go) | `expr.c` `yield_typecheck`; `ty/expr.c` `check_yield` |  |
| `checkAwaitExpression` (checker.go) | `ty/call.c` `ty_check_await_operand`; `expr.c` `await_check_grammar` |  |
| `checkDeleteExpressionMustBeOptional` (checker.go) | `ty/check.c` `ty_check_delete_operand` |  |
| `checkInExpression` (checker.go) | `ty/check.c` `ty_check_in_expression` |  |
| `checkInstanceOfExpression` (checker.go) | `ty/check.c` `ty_check_instanceof_has_instance`; `expr.c` `instanceof_target_ok` |  |
| `checkSatisfiesExpression` (checker.go) | `ty/check.c` `ty_check_satisfies` |  |
| `checkAssertion` (checker.go) | `ty/check.c` `ty_check_type_assertion` |  |
| `checkFunctionExpressionOrObjectLiteralMethod` (checker.go) | `function_impl.c` `function_impl_typecheck`; `ty/expr.c` `settle_function` |  |
| `contextuallyCheckFunctionExpressionOrObjectLiteralMethod` (checker.go) | `ty/call.c` `ty_fix_contextual_signature`; `ty/expr.c` `assign_contextual_signature_as`; `ty/expr.c` `settle_function` | contextual parameter types are assigned when the function is settled |
| `checkTypeArguments` (checker.go) | `ty/expr.c` `apply_type_arguments_part`; `ty/call.c` `check_type_argument_constraints`; `expr.c` `check_type_argument_constraints` |  |
| `checkJsxChildren` (jsx.go) | `ty/expr.c` `check_jsx_child`; `ty/jsx.c` `check_children` |  |
| `checkJsxElement` (jsx.go) | `ty/jsx.c` `ty_check_jsx_element`; `expr.c` `jsx_element_typecheck` |  |
| `getFlowTypeOfAccessExpression` (checker.go) | `ty/expr.c` `access_flow_type` |  |
| `checkPropertyNotUsedBeforeDeclaration` (checker.go) | `ty/expr.c` `ty_check_property_used_before_assigned`; `ty/check.c` `check_class_used_before_declaration` |  |
| `getSuggestedSymbolForNonexistentProperty` (checker.go) | `ty/check.c` `ty_suggested_property` |  |

## Contextual typing (checker.go)
| tsgo | tttt | notes |
|---|---|---|
| `getContextualType` (checker.go) | `ty/expr.c` `ty_check_expr` | tttt passes the contextual type down (`ExprCtx.contextual`, set by the parent: `contextual_prop_type`, `contextual_array_element_type`...) rather than walking up from the node |
| `getApparentTypeOfContextualType` (checker.go) | `ty/expr.c` `apparent_contextual_type` |  |
| `instantiateContextualType` (checker.go) | `ty/call.c` `ty_instantiate_contextual_type`; `ty/call.c` `ty_instantiate_contextual_type_for` |  |
| `getContextualTypeForObjectLiteralElement` (checker.go) | `ty/expr.c` `get_prop_context`; `ty/expr.c` `contextual_prop_type`; `ty/expr.c` `ty_contextual_prop_type` |  |
| `getContextualTypeForElementExpression` (checker.go) | `ty/expr.c` `get_element_context`; `ty/expr.c` `contextual_array_element_type`; `ty/expr.c` `ty_contextual_element_type` |  |
| `getContextualSignature` (checker.go) | `ty/expr.c` `ty_contextual_signature`; `ty/expr.c` `contextual_signature` |  |
| `getContextualReturnType` (checker.go) | `ty/expr.c` `ty_contextual_return_type`; `ty/expr.c` `ty_contextual_return_type_self`; `function_impl.c` `function_impl_enter_return_context` |  |
| `getContextualThisParameterType` (checker.go) | `ty/expr.c` `ty_contextual_this_type` |  |
| `getContextuallyTypedParameterType` (checker.go) | `ty/expr.c` `ty_contextual_parameter_type`; `ty/expr.c` `ty_contextual_parameter_type_unwidened` |  |
| `isContextSensitive` (checker.go) | `ty/expr.c` `ty_expr_is_context_sensitive`; `ty/expr.c` `ty_function_is_context_sensitive`; `ty/expr.c` `has_context_sensitive_params` |  |
| `isLiteralOfContextualType` (checker.go) | `ty/expr.c` `ty_is_literal_of_contextual_type`; `ty/expr.c` `is_literal_of_contextual_type` |  |
| `checkExpressionForMutableLocation` (checker.go) | `ty/expr.c` `ty_mutable_location_type`; `ty/expr.c` `check_for_mutable_location` |  |

## Control flow (flow.go)
| tsgo | tttt | notes |
|---|---|---|
| `getFlowTypeOfReference` (flow.go) | `ty/flow.c` `ty_flow_type`; `ty/expr.c` `identifier_flow_type`; `ty/expr.c` `access_flow_type` |  |
| `getTypeAtFlowNode` (flow.go) | `ty/flow.c` `type_at_flow_node` | memo per (reference, flow node) in the checker's links |
| `getTypeAtFlowAssignment` (flow.go) | `ty/flow.c` `type_at_flow_assignment` |  |
| `getTypeAtFlowCondition` (flow.go) | `ty/flow.c` `type_at_flow_condition` |  |
| `getTypeAtFlowBranchLabel` (flow.go) | `ty/flow.c` `type_at_branch_label`; `ty/flow.c` `union_at_label` |  |
| `getTypeAtFlowLoopLabel` (flow.go) | `ty/flow.c` `type_at_loop_label` | loop stack in `loop_stack_push` |
| `getTypeAtFlowCall` (flow.go) | `ty/flow.c` `type_at_flow_call` |  |
| `getTypeAtSwitchClause` (flow.go) | `ty/flow.c` `type_at_switch_clause` |  |
| `narrowType` (flow.go) | `ty/flow.c` `narrow_type` |  |
| `narrowTypeByTruthiness` (flow.go) | `ty/flow.c` `narrow_by_truthiness` |  |
| `narrowTypeByEquality` (flow.go) | `ty/flow.c` `narrow_by_equality` |  |
| `narrowTypeByTypeof` (flow.go) | `ty/flow.c` `narrow_by_typeof`; `ty/flow.c` `narrow_by_type_name` |  |
| `narrowTypeByInstanceof` (flow.go) | `ty/flow.c` `narrow_by_instanceof` |  |
| `narrowTypeByInKeyword` (flow.go) | `ty/flow.c` `narrow_by_in` |  |
| `narrowTypeByDiscriminantProperty` (flow.go) | `ty/flow.c` `narrow_by_discriminant`; `ty/flow.c` `keep_by_discriminant` |  |
| `narrowTypeByCallExpression` (flow.go) | `ty/flow.c` `narrow_by_call`; `ty/flow.c` `narrow_by_predicate` |  |
| `narrowTypeBySwitchOnDiscriminant / on typeof` (flow.go) | `ty/flow.c` `narrow_by_switch_on_discriminant`; `ty/flow.c` `narrow_by_switch_on_typeof` |  |
| `getNarrowedType` (flow.go) | `ty/flow.c` `narrowed_type`; `ty/flow.c` `narrowed_type_uncached`; `ty/flow.c` `narrow_generic_related` |  |
| `isMatchingReference` (flow.go) | `ty/flow.c` `is_matching_reference`; `ty/flow.c` `query_matches` |  |
| `isDiscriminantProperty` (relater.go) | `ty/flow.c` `is_discriminant_property`; `ty/flow.c` `ty_is_discriminant_property` |  |
| `getKeyPropertyName / mapTypesByKeyProperty` (relater.go) | `ty/flow.c` `ty_union_key_property`; `ty/flow.c` `map_by_key_property` |  |
| `isConstantReference` (flow.go) | `ty/flow.c` `is_constant_reference`; `ty/flow.c` `constant_reference_type` |  |
| `getAssignedType` (flow.go) | `ty/flow.c` `for_each_element_type`; `ty/flow.c` `initial_or_assigned_type` |  |
| `getInitialOrAssignedType` (flow.go) | `ty/flow.c` `initial_or_assigned_type` |  |
| `isReachableFlowNode` (flow.go) | `ty/flow.c` `ty_flow_reachability`; `ty/flow.c` `reachability_walk` |  |
| `isExhaustiveSwitchStatement` (flow.go) | `ty/flow.c` `is_exhaustive_switch` |  |
| `getEvolvingArrayType / autoArray` (flow.go) | `ty/flow.c` `evolving_array`; `ty/flow.c` `final_array_type` |  |

## Statements and declarations (checker.go)
| tsgo | tttt | notes |
|---|---|---|
| `checkSourceFile` (checker.go) | `program.c` `program_typecheck`; `program.c` `check_file`; `statement.c` `statements_typecheck` | files checked in parallel on several checkers; bodies of functions checked when their declaration is |
| `checkSourceElement` (checker.go) | `statement.c` `statement_typecheck`; `statement.c` `statement_typecheck_inner` |  |
| `checkSourceElementUnreachable` (checker.go) | `statement.c` `statement_check_unreachable`; `statement.c` `statement_unreachable_by_flow` |  |
| `checkClassLikeDeclaration` (checker.go) | `class_decl.c` `class_decl_typecheck`; `class_decl.c` `class_decl_typecheck_members`; `class_decl.c` `class_heritage_typecheck` |  |
| `checkInterfaceDeclaration` (checker.go) | `interface_decl.c` `interface_decl_typecheck`; `interface_decl.c` `interface_heritage_typecheck` |  |
| `checkTypeAliasDeclaration` (checker.go) | `statement.c` `type_decl_typecheck` |  |
| `checkEnumDeclaration` (checker.go) | `enum_decl.c` `enum_decl_typecheck`; `ty/enums.c` `ty_resolve_enum` |  |
| `checkModuleDeclaration` (checker.go) | `statement.c` `namespace_decl_typecheck` |  |
| `checkImportDeclaration / checkExportDeclaration` (checker.go) | `port_statement.c` `port_statement_typecheck` |  |
| `checkFunctionDeclaration` (checker.go) | `function_impl.c` `function_impl_typecheck`; `function_impl.c` `function_impl_typecheck_body` |  |
| `checkSignatureDeclaration` (checker.go) | `function_impl.c` `function_impl_typecheck_signature` |  |
| `checkReturnStatement` (checker.go) | `statement.c` `return_typecheck` |  |
| `checkRightHandSideOfForOf` (checker.go) | `statement.c` `for_of_iterable`; `statement.c` `for_each_loop_statement_evaluate_var_type` |  |
| `checkVariableStatement` (checker.go) | `statement.c` `variable_statement_typecheck` |  |
| `checkBlock` (checker.go) | `block.c` `block_typecheck` |  |
| `checkTypePredicate` (checker.go) | `type.c` `type_typecheck`; `function_impl.c` `function_impl_typecheck_signature` |  |
| `checkTypeReferenceOrImport` (checker.go) | `type.c` `type_typecheck`; `several_types.c` `identifier_type_typecheck` |  |
| `checkUnusedIdentifiers` (checker.go) | `unused.c` `unused_report` |  |
| `checkMembersForOverrideModifier` (checker.go) | `class_decl.c` `class_check_member_overrides`; `class_decl.c` `check_member_override` |  |
| `checkIndexConstraints` (checker.go) | `ty/check.c` `ty_check_index_constraints` |  |
| `checkDeferredNodes` (checker.go) | `function_impl.c` `function_impl_typecheck_body` | no deferred queue: a function body is checked with its declaration (CLI additionally defers *parsing* bodies: program.c `deferred_bodies_*`) |

## Type printing and node builder (nodebuilder*.go, printer)
| tsgo | tttt | notes |
|---|---|---|
| `typeToString` (printer.go) | `ty/ty_display.c` `ty_display`; `ty/ty_display.c` `ty_to_cstr` | printer works on `Ty` directly, no synthesized TypeNode |
| `typeToTypeNode` (nodebuilderimpl.go) | `ty/ty_display.c` `ty_display`; `ty/ty_display.c` `display_union`; `ty/ty_display.c` `display_mapped`; `ty/ty_display.c` `display_signature` |  |
| `createAnonymousTypeNodeEx` (nodebuilderimpl.go) | `ty/ty_display.c` `ty_display`; `ty/ty_display.c` `anonymous_type_query` |  |
| `tryReuseExistingNodeHelper` (nodecopy.go) | `ty/ty_display.c` `display_annotated`; `ty/ty_display.c` `reuses_nodes`; `ty/ty_display.c` `display_node_impl` | reuse of written type nodes (`display_node_*`) |
| `serializeTypeName` (nodebuilderimpl.go) | `ty/ty_display.c` `display_node_identifier`; `ty/ty_display.c` `display_node_typeof` |  |
| `trackComputedName` (nodebuilderimpl.go) | `ty/declaration_emit.c` `track_computed_name`; `ty/ty_display.c` `index_components_printed` |  |

## Diagnostics and elaboration (checker.go)
| tsgo | tttt | notes |
|---|---|---|
| `elaborateError` (relater.go) | `ty/check.c` `elaborate_error`; `ty/check.c` `report_unassignable` |  |
| `elaborateElement` (relater.go) | `ty/check.c` `elaborate_element` |  |
| `elaborateObjectLiteral` (relater.go) | `ty/check.c` `elaborate_object_literal` |  |
| `elaborateArrayLiteral` (relater.go) | `ty/check.c` `elaborate_array_literal` |  |
| `elaborateJsxComponents` (jsx.go) | `ty/check.c` `elaborate_jsx_attributes`; `ty/check.c` `elaborate_jsx_children` |  |
| `generateJsxChildren` (jsx.go) | `ty/check.c` `elaborate_jsx_children` |  |
| `elaborateArrowFunction` (relater.go) | `ty/check.c` `elaborate_arrow_function` |  |

No direct counterpart found (or too diffuse to map): `getTypeOfSymbolWorker` as a separate step (folded into `ty_type_of_symbol`), `checkDeferredNodes` queue (see its row), `getExportsOfModule`'s per-symbol links (tttt resolves `export *` on first use), `isDeclarationContainedBy`, `getRecursionIdentityTarget`, `getAccessedPropertyName`, `hasPrecedingLineBreak`/`nextIsNotDot`/`createIdentifier`/`parseExportDeclaration` (the parser has no node factory or scanner state; its helpers are listed under Parser).

