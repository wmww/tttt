#include "filesystem.h"
#include "failable.h"
#include "common_macros.h"
#include "string_helpers.h"
#include "regex_helpers.h"
#include "test_docs_code.h"
#include "test.h"

typedef struct CodeBlockTest CodeBlockTest;
struct CodeBlockTest {
    TestDocsCodeBlockImpl impl;
    cstr code_block;
    TestDocsCodeBlockData* data;
};

void test_code_block_impl(TestCtx* test_ctx, void const* test_data) {
    CodeBlockTest const* data = test_data;
    cstr_drop(&data->data->code_path);
    data->data->code_path = cstr_lit("/tmp/cleat-docs-test.c");
    try (fs_write_file(fs, cstr_sv(&data->data->code_path), cstr_sv(&data->code_block))) test_ok_or_bail;
    data->impl(test_ctx, data->data);
    (void)fs_remove(fs, cstr_sv(&data->data->code_path), false);
    cstr_take(&data->data->code_path, cstr_null);
}

void test_code_block_drop(void* test_data) {
    CodeBlockTest* me = test_data;
    cstr_drop(&my code_block);
    cstr_drop(&my data->docs_path);
    cstr_drop(&my data->language);
    cstr_drop(&my data->code_path);
    free(my data);
    free(me);
}

void test_docs_code_register_markdown_file(csview test_module, csview path, TestDocsCodeBlockImpl impl) {
    cstr markdown;
    try (fs_read_file(fs, path, &markdown)) if_err (e) {
        register_test_problem(test_module, c_sv("reading doc file"), error_base_get_message(e));
        current_error_destroy();
        return;
    }
    bool in_code_block = false;
    csview language = csview_null;
    i64 start_line = 0;
    cstr code_block = cstr_null;
    for_each (line, lines_cstr, &markdown) {
        csview trimmed = csview_trim(line);
        if (csview_starts_with(trimmed, "```")) {
            in_code_block = !in_code_block;
            if (in_code_block) {
                language = csview_without_prefix(trimmed, "```");
                start_line = line_index;
            } else {
                cstr test_name = cstr_from_fmt("code block on line %lld", (long long)start_line);
                register_test(
                    test_module,
                    cstr_sv(&test_name),
                    test_code_block_impl,
                    test_code_block_drop,
                    c_new(CodeBlockTest, {
                        .impl = impl,
                        .code_block = cstr_move(&code_block),
                        .data = c_new(TestDocsCodeBlockData, {
                            .docs_path = cstr_from_sv(path),
                            .docs_line = start_line,
                            .language = cstr_from_sv(language),
                            .code_path = cstr_null, // set later
                        }),
                    }
                ));
                cstr_drop(&test_name);
            }
        } else if (in_code_block) {
            cstr_append_sv(&code_block, line);
            cstr_append_lit(&code_block, "\n");
        } else {
            cregex re = cregex_make("\\]\\(([^\\)]+)\\)", CREG_DEFAULT);
            for_each (groups, cregex, &re, line) {
                csview link = groups[1];
                if (csview_starts_with(link, "https://")) break;
                try (cstr path_absolute, fs_absolute_path_of(fs, path, &path_absolute)) or_panic;
                cstr link_absolute = absolute_path_of(dirname_of(cstr_sv(&path_absolute)), link);
                try (cstr cwd, fs_get_cwd(fs, &cwd)) or_panic;
                cstr link_relative = relative_path_of(cstr_sv(&cwd), cstr_sv(&link_absolute));
                if (!fs_exists(fs, cstr_sv(&link_relative))) {
                    register_test_problem(
                        test_module,
                        c_sv("links valid"),
                        cstr_from_fmt(
                            "%.*s:%lld: broken link to %s",
                            c_SV(path), (long long)start_line, cstr_str(&link_relative)
                        )
                    );
                }
                c_drop(cstr, &path_absolute, &link_absolute, &link_relative, &cwd);
            }
            cregex_drop(&re);
        }
    }
    cstr_drop(&markdown);
    cstr_drop(&code_block);
    if (in_code_block) {
        register_test_problem(
            test_module,
            c_sv("parsing doc file"),
            cstr_from_fmt("%.*s:%lld: unclosed code block", c_SV(path), (long long)start_line)
        );
    }
}

void test_docs_code_register_directory(csview directory, TestDocsCodeBlockImpl impl) {
    i64 files_found = 0;
    for_each (item, fs_listdir, fs, directory) {
        if (csview_equals(file_extension_of(item.name), "md") ||
            csview_equals(file_extension_of(item.name), "MD")
        ) {
            files_found++;
            test_docs_code_register_markdown_file(item.name, item.path, impl);
        }
    }
    if (files_found == 0) {
        try (cstr abs_path, fs_absolute_path_of(fs, directory, &abs_path)) if_err (e) {
            abs_path = cstr_from_sv(directory);
        }
        register_test_problem(
            directory,
            c_sv("enumerating doc files"),
            cstr_from_fmt("no documentation files found in %s", cstr_str(&abs_path))
        );
        cstr_drop(&abs_path);
    }
}
