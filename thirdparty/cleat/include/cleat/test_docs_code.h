#pragma once

#include <stc/csview.h>

typedef struct TestCtx TestCtx;

typedef struct TestDocsCodeBlockData {
    cstr docs_path;
    i64 docs_line;
    cstr language;
    cstr code_path;
} TestDocsCodeBlockData;
typedef void (*TestDocsCodeBlockImpl)(TestCtx* test_ctx, TestDocsCodeBlockData const* data);
void test_docs_code_register_markdown_file(csview test_module, csview path, TestDocsCodeBlockImpl impl);
void test_docs_code_register_directory(csview directory, TestDocsCodeBlockImpl impl);
