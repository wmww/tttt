#include "baseline_test_result.h"
#include "baseline_test_suite.h"
#include "baseline_test_case.h"
#include "subprocess.h"
#include "value.h"
#include "result.h"

#include <cleat/filesystem.h>

BaselineTestResult make_baseline_test_result(cstr source_path, cstr config) {
    csview base = test_name_from_path(cstr_sv(&source_path));
    return (BaselineTestResult){
        .name = cstr_size(&config) ? cstr_d(base, "(", config, ")") : cstr_from_sv(base),
        .source_path = source_path,
        .config = config,
    };
}

void baseline_test_result_drop(BaselineTestResult* me) {
    cstr_drop(&my source_path);
    cstr_drop(&my config);
    cstr_drop(&my name);
}

static bool compare_outputs(
    csview test_name,
    const char* output_name,
    csview path_of_expected,
    csview our_result,
    bool show_diff_on_error
) {
    cstr expected = cstr_null;
    if (fs_is_reg_file(fs, path_of_expected)) {
        try (fs_read_file(fs, path_of_expected, &expected)) if_err (e) {
            // A missing or unreadable baseline just means the test component fails
            if (show_diff_on_error) {
                println(test_name, ": ", path_of_expected, " could not be read");
                error_base_eprintln(e);
            }
            current_error_destroy();
            return false;
        }
    }

    csview expected_sv = cstr_sv(&expected);
    if (csview_cmp(&our_result, &expected_sv) == 0) {
        cstr_drop(&expected);
        return true;
    } else {
        cstr result_display = cstr_d("tttt ", output_name, ":\n", our_result, "\n");
        cstr expected_display = cstr_d(path_of_expected, ":\n", expected_sv, "\n");

        if (show_diff_on_error) {
            println(test_name, ": ", output_name, " did not match:");
            run_diff_subprocess(cstr_sv(&result_display), cstr_sv(&expected_display));
        }

        c_drop(cstr, &expected, &result_display, &expected_display);
        return false;
    }
}

bool baseline_show_dts = false;

// A `//// [name]\r\n` section of a `.js` baseline: its name and its whole text (header included)
typedef struct JsSection {
    csview name;
    csview text;
} JsSection;

static bool is_declaration_output_name(csview name) {
    return csview_ends_with(name, ".d.ts") || csview_ends_with(name, ".d.mts") || csview_ends_with(name, ".d.cts");
}

// The declaration files of a `.js` baseline (tsgo's DoJSEmitBaseline): after the `inputs` input files' sections come the
// JavaScript outputs, then the declaration outputs, then maybe a `DtsFileErrors` section and `!!!! File` notes. Returns
// how many sections it found (at most `cap`)
static u32 js_baseline_declarations(csview text, u32 inputs, JsSection* out, u32 cap) {
    u32 seen = 0, found = 0;
    char const* end = text.buf + text.size;
    JsSection* open = nullptr;
    for (char const* line = text.buf; line < end;) {
        char const* eol = memchr(line, '\n', (size_t)(end - line));
        char const* next = eol ? eol + 1 : end;
        csview l = { line, next - line };
        bool header = csview_starts_with(l, "//// [") && csview_ends_with(l, "]\r\n");
        bool stop = csview_starts_with(l, "!!!! File ") || (header && csview_equals(l, "//// [DtsFileErrors]\r\n"));
        if (header || stop) {
            // (The blank lines the harness puts between groups belong to no file)
            if (open) {
                char const* to = line;
                if (to - open->text.buf >= 4 && !memcmp(to - 4, "\r\n\r\n", 4)) to -= 4;
                open->text.size = to - open->text.buf;
                open = nullptr;
            }
            if (stop) break;
            csview name = { line + 6, l.size - 6 - 3 };
            if (seen++ >= inputs && (found || is_declaration_output_name(name)) && found < cap) {
                out[found] = (JsSection){ name, { line, end - line } };
                open = &out[found++];
            }
        }
        line = next;
    }
    return found;
}

// The output name tsgo's harness shows for a TypeScript file's declarations (outputpaths.ChangeToDeclarationExtension)
static cstr declaration_output_name(csview path) {
    csview base = basename_of(path);
    if (csview_ends_with(base, ".mts")) return cstr_d(csview_slice(base, 0, base.size - 4), ".d.mts");
    if (csview_ends_with(base, ".cts")) return cstr_d(csview_slice(base, 0, base.size - 4), ".d.cts");
    isize dot = base.size;
    while (dot > 0 && base.buf[dot - 1] != '.') dot--;
    return cstr_d(dot ? csview_slice(base, 0, dot - 1) : base, ".d.ts");
}

// The dts component: tttt's declaration outputs against those of the `.js` baseline, in the baseline's order
static void run_dts(BaselineTestResult* me, BaselineTestCase const* test_case, csview js_path) {
    if (!fs_is_reg_file(fs, js_path)) return;
    cstr expected = cstr_null;
    try (fs_read_file(fs, js_path, &expected)) if_err (e) {
        ignore e;
        current_error_destroy();
        return;
    }
    u32 inputs = 0;
    bool js_inputs = false;
    for_each (i, vec_baseline_test_file, &test_case->files) {
        if (test_case->has_tsconfig && i->config_source) continue;
        inputs++;
        js_inputs |= i->source && i->source->is_js && !i->source->is_json;
    }
    enum { CAP = 256 };
    JsSection* sections = c_new_n(JsSection, CAP);
    u32 count = js_baseline_declarations(cstr_sv(&expected), inputs, sections, CAP);
    if (count) {
        my has_dts = !js_inputs;
        my dts_js = js_inputs;
    }
    if (!my has_dts) {
        free(sections);
        cstr_drop(&expected);
        return;
    }
    // Ours: each test file's declaration output, by output name
    cstr expected_text = cstr_null, ours = cstr_null;
    bool* used = calloc((size_t)vec_baseline_test_file_size(&test_case->files) + 1, sizeof(bool));
    for (u32 k = 0; k < count; k++) {
        cstr_append_sv(&expected_text, sections[k].text);
        for_each (i, vec_baseline_test_file, &test_case->files) {
            if (used[i_index] || i->superseded || !i->source || !i->source->declaration_output) continue;
            cstr name = declaration_output_name(cstr_sv(&i->path));
            bool same = csview_equals_sv(cstr_sv(&name), sections[k].name);
            cstr_drop(&name);
            if (!same) continue;
            used[i_index] = true;
            cstr_append_d(&ours, DEFAULT_DISPLAY, "//// [", sections[k].name, "]\r\n", *i->source->declaration_output);
            break;
        }
    }
    // (What tttt emits that the baseline doesn't have, after)
    for_each (i, vec_baseline_test_file, &test_case->files) {
        if (used[i_index] || i->superseded || !i->source || !i->source->declaration_output) continue;
        cstr name = declaration_output_name(cstr_sv(&i->path));
        cstr_append_d(&ours, DEFAULT_DISPLAY, "//// [", name, "]\r\n", *i->source->declaration_output);
        cstr_drop(&name);
    }
    free(used);
    free(sections);
    csview ours_sv = cstr_sv(&ours), expected_sv = cstr_sv(&expected_text);
    my dts_passed = csview_equals_sv(ours_sv, expected_sv);
    if (!my dts_passed && baseline_show_dts) {
        println(my name, ": dts did not match:");
        cstr result_display = cstr_d("tttt dts:\n", ours_sv, "\n");
        cstr expected_display = cstr_d(js_path, ":\n", expected_sv, "\n");
        run_diff_subprocess(cstr_sv(&result_display), cstr_sv(&expected_display));
        c_drop(cstr, &result_display, &expected_display);
    }
    c_drop(cstr, &expected, &expected_text, &ours);
}

static void dump_output(BaselineTestSuite* suite, csview name, const char* extension, csview contents) {
    if (!csview_size(suite->dump_dir)) return;
    cstr path = cstr_d(suite->dump_dir, "/", name, extension);
    try (fs_write_file(fs, cstr_sv(&path), contents)) or_log(error);
    cstr_drop(&path);
}

void baseline_test_result_run(
    BaselineTestResult* me,
    BaselineTestSuite* suite,
    JsObject const* previous,
    bool show_errs,
    bool show_syms,
    bool show_typs
) {
    Result errors_prev  = previous ? js_object_get(previous, c_sv("errors"))  : result_ok(value_boolean(false));
    Result symbols_prev = previous ? js_object_get(previous, c_sv("symbols")) : result_ok(value_boolean(false));
    Result types_prev   = previous ? js_object_get(previous, c_sv("types"))   : result_ok(value_boolean(false));
    Result dts_prev     = previous ? js_object_get(previous, c_sv("dts"))     : result_ok(value_undefined);

    my has_prev = previous != nullptr;
    my errors_prev = value_to_boolean(&errors_prev.value);
    my symbols_prev = value_to_boolean(&symbols_prev.value);
    my types_prev = value_to_boolean(&types_prev.value);
    my has_dts_prev = dts_prev.value.tag == VALUE_BOOLEAN;
    my dts_prev = value_to_boolean(&dts_prev.value);

    result_drop(&errors_prev);
    result_drop(&symbols_prev);
    result_drop(&types_prev);
    result_drop(&dts_prev);

    bool noisy = show_errs || show_syms || show_typs;
    csview name = cstr_sv(&my name);
    // tsgo's layout: testdata/tests/cases/compiler/x.ts has its baselines in testdata/baselines/reference/compiler
    // tttt's own tests (BaselineTestSuite.own_dir) have theirs in own_dir/baselines
    csview kind = csview_contains(cstr_sv(&my source_path), "/cases/compiler/") ? c_sv("compiler") : c_sv("conformance");
    csview path = cstr_sv(&my source_path);
    bool own = csview_size(suite->own_dir) && csview_size(path) > csview_size(suite->own_dir) &&
        csview_equals_sv(csview_slice(path, 0, csview_size(suite->own_dir)), suite->own_dir) &&
        csview_starts_with(csview_slice(path, csview_size(suite->own_dir), INTPTR_MAX), "/cases/");
    cstr baselines = own ? cstr_d(suite->own_dir, "/baselines/") :
        cstr_d(suite->tests_dir, "/../baselines/reference/", kind, "/");
    cstr errors_path = cstr_d(baselines, name, ".errors.txt");
    cstr symbols_path = cstr_d(baselines, name, ".symbols");
    cstr types_path = cstr_d(baselines, name, ".types");
    cstr js_path = cstr_d(baselines, name, ".js");
    cstr_drop(&baselines);

    // It's ok if errors file doesn't exist, and with @noTypesAndSymbols the other two don't either. Whether a test
    // has that is only known after loading it, so a test without baselines runs and is then dropped if it turns out
    // it should have had them (a handful of tests tsc itself doesn't produce baselines for)
    bool has_baselines = fs_is_reg_file(fs, cstr_sv(&symbols_path)) && fs_is_reg_file(fs, cstr_sv(&types_path));

    bool dumping = csview_size(suite->dump_dir) != 0;
    if (dumping) {
        // Lets whatever reads our stderr attribute the log lines that follow to a test
        eprintln("### TEST ", name);
    }

    BaselineTestCase test_case = run_baseline_test_case(
        &suite->cache, cstr_sv(&my source_path), cstr_sv(&my config), noisy, dumping);
    my skipped = test_case.skipped;
    my errors_only = test_case.no_types_and_symbols;
    my did_run = test_case.did_run && (has_baselines || my errors_only);
    if (test_case.did_run && !my did_run && noisy) {
        eprintln(color_error, name, ":", color_normal, " ", symbols_path, " or ", types_path, " does not exist");
    }
    // An own test where tsgo reported nothing has an empty NAME.noerrors instead of NAME.errors.txt
    // (scripts/owntests.sh), so that a regeneration that went wrong can't pass as a clean test
    if (my did_run && own && !fs_is_reg_file(fs, cstr_sv(&errors_path))) {
        cstr marker = cstr_d(suite->own_dir, "/baselines/", name, ".noerrors");
        if (!fs_is_reg_file(fs, cstr_sv(&marker))) {
            my did_run = false;
            if (noisy) {
                eprintln(color_error, name, ":", color_normal, " neither ", errors_path, " nor ", marker,
                    " exists (scripts/owntests.sh)");
            }
        }
        cstr_drop(&marker);
    }

    if (my did_run) {
        cstr our_result = cstr_null;

        baseline_test_case_render_errors(&our_result, &test_case);
        dump_output(suite, name, ".errors.txt", cstr_sv(&our_result));
        my errors_passed = compare_outputs(
            name,
            "errors",
            cstr_sv(&errors_path),
            cstr_sv(&our_result),
            show_errs
        );
        cstr_drop(&our_result);
        // tsgo's `.contentmapper` baseline (plan 56) is part of the errors component: the content-mapped files'
        // diagnostics are there instead
        cstr content_mapper_path = cstr_d(cstr_sv(&errors_path), "");
        cstr_replace(&content_mapper_path, ".errors.txt", ".contentmapper");
        if (cstr_size(&test_case.content_mapper_baseline) || fs_is_reg_file(fs, cstr_sv(&content_mapper_path))) {
            dump_output(suite, name, ".contentmapper", cstr_sv(&test_case.content_mapper_baseline));
            my errors_passed &= compare_outputs(name, "contentmapper", cstr_sv(&content_mapper_path),
                cstr_sv(&test_case.content_mapper_baseline), show_errs);
        }
        cstr_drop(&content_mapper_path);
    }

    if (my did_run && my errors_only) {
        my symbols_passed = true;
        my types_passed = true;
    } else if (my did_run) {
        cstr our_result = cstr_null;

        baseline_test_case_render_symbols(&our_result, &test_case);
        dump_output(suite, name, ".symbols", cstr_sv(&our_result));
        my symbols_passed = compare_outputs(
            name,
            "symbols",
            cstr_sv(&symbols_path),
            cstr_sv(&our_result),
            show_syms
        );
        cstr_clear(&our_result);

        // (tsgo's hasErrorBaseline: the program has diagnostics, the content-mapped files' too)
        baseline_test_case_render_types(&our_result, &test_case,
            fs_is_reg_file(fs, cstr_sv(&errors_path)) || test_case.content_mapper_errors);
        dump_output(suite, name, ".types", cstr_sv(&our_result));
        my types_passed = compare_outputs(
            name,
            "types",
            cstr_sv(&types_path),
            cstr_sv(&our_result),
            show_typs
        );
        cstr_drop(&our_result);
    }

    if (my did_run) {
        run_dts(me, &test_case, cstr_sv(&js_path));
        {
            cstr ours = cstr_null;
            for_each (i, vec_baseline_test_file, &test_case.files) {
                if (i->superseded || !i->source || !i->source->declaration_output) continue;
                cstr output_name = declaration_output_name(cstr_sv(&i->path));
                cstr_append_d(&ours, DEFAULT_DISPLAY, "//// [", output_name, "]\r\n", *i->source->declaration_output);
                cstr_drop(&output_name);
            }
            if (cstr_size(&ours)) dump_output(suite, name, ".d.ts", cstr_sv(&ours));
            cstr_drop(&ours);
        }
    }

    baseline_test_case_drop(&test_case);

    c_drop(cstr,
        &js_path,
        &errors_path,
        &symbols_path,
        &types_path,
    );
}

bool baseline_test_result_is_pass(BaselineTestResult const* me) {
    return my errors_passed && my symbols_passed && my types_passed;
}

bool baseline_test_result_was_pass(BaselineTestResult const* me) {
    return my errors_prev && my symbols_prev && my types_prev;
}

bool baseline_test_result_has_new_pass(BaselineTestResult const* me) {
    return (my errors_passed  && (!my has_prev || !my errors_prev )) ||
           (my symbols_passed && (!my has_prev || !my symbols_prev)) ||
           (my types_passed   && (!my has_prev || !my types_prev  )) ||
           (my has_dts && my dts_passed && (!my has_dts_prev || !my dts_prev));
}

bool baseline_test_result_has_new_fail(BaselineTestResult const* me) {
    return (!my errors_passed  && my errors_prev ) ||
           (!my symbols_passed && my symbols_prev) ||
           (!my types_passed   && my types_prev  ) ||
           (my has_dts_prev && my dts_prev && !(my has_dts && my dts_passed));
}
