core_files = files(
    'prelude.c',
    'span.c',
    'interface.c',
    'type.c',
    'lazy_type.c',
    'interface_member.c',
    'callable_type.c',
    'generic.c',
    'identifier_type.c',
    'ternary_type.c',
    'indexed_type.c',
    'class_decl.c',
    'type_decl.c',
    'block.c',
    'expr.c',
    'value.c',
    'statement.c',
    'import_statement.c',
    'binding.c',
    'function_impl.c',
    'display.c',
    'parser.c',
    'error.c',
    'name_stack.c',
    'filesystem.c',
    'engine.c',
    'string_helpers.c',
)

main_files = core_files + files('main.c')

test_files = core_files + files(
    'test/baseline_test_suite.c',
    'test/baseline_test_result.c',
    'test/baseline_test_case.c',
    'test/baseline_test_traversal.c',
    'test/subprocess.c',
)

baseline_test_files = test_files + files('test/baseline_test_main.c')
diffapp_runner_files = test_files + files('test/diffapp_runner.c')

src_inc = [ include_directories('.') ]
