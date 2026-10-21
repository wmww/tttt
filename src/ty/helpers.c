#include "ty/ty_internal.h"
#include "program.h"
#include "source_file.h"
#include "compiler_options.h"
#include "error.h"

// tsc's checkExternalEmitHelpers: with `importHelpers`, syntax that the emitter lowers into a call of a tslib helper
// needs that helper to be exported by the "tslib" the file imports implicitly (SourceFile.helpers_ref)

static const char* const helper_names[][2] = {
    [0] = { "__extends" },
    [1] = { "__assign" },
    [2] = { "__rest" },
    [3] = { "__decorate" }, // __esDecorate, __runInitializers without experimentalDecorators
    [4] = { "__metadata" },
    [5] = { "__param" },
    [6] = { "__awaiter" },
    [7] = { "__generator" },
    [8] = { "__values" },
    [9] = { "__read" },
    [10] = { "__spreadArray" },
    [11] = { "__await" },
    [12] = { "__asyncGenerator" },
    [13] = { "__asyncDelegator" },
    [14] = { "__asyncValues" },
    [15] = { "__exportStar" },
    [16] = { "__importStar" },
    [17] = { "__importDefault" },
    [18] = { "__makeTemplateObject" },
    [19] = { "__classPrivateFieldGet" },
    [20] = { "__classPrivateFieldSet" },
    [21] = { "__classPrivateFieldIn" },
    [22] = { "__setFunctionName" },
    [23] = { "__propKey" },
    [24] = { "__addDisposableResource", "__disposeResources" },
};

// What a helper's signatures need more than, when there were older versions of it with fewer parameters
static u32 helper_min_params(u32 helper) {
    switch (helper) {
        case EMIT_HELPER_CLASS_PRIVATE_FIELD_GET: return 3;
        case EMIT_HELPER_CLASS_PRIVATE_FIELD_SET: return 4;
        case EMIT_HELPER_SPREAD_ARRAY: return 2;
        default: return 0;
    }
}

static bool has_signature_with_more_params(Checker* me, Symbol const* symbol, u32 than) {
    Members const* members = ty_members(me, ty_type_of_symbol(me, symbol));
    for (u32 i = 0; i < members->call_count; i++) {
        signature_resolve(me, members->call[i]);
        if (sig_res(members->call[i])->param_count > than) return true;
    }
    return false;
}

void ty_check_emit_helpers(Checker* me, Span at, u32 helpers, ErrorCtx* errs) {
    SourceFile const* file = span_source(at);
    if (!file || !file->helpers_ref) return;
    ModuleRef* ref = file->helpers_ref;
    Symbol* module = binder_module_of_ref(my binder, ref);
    if (!module) {
        // Said once per file (tsc's links.externalHelpersModule)
        u64 key[2] = { KEY_EMIT_HELPERS, (u64)(uintptr_t)file };
        void const** reported = intern_slot(me, &my links, key, 2);
        if (!*reported) {
            *reported = me;
            // (tsc's resolveExternalModule: a module that is JavaScript says so instead, under noImplicitAny)
            if (ref->failure == MODULE_REF_IS_JS) {
                module_ref_report_implicit_any_at(ref, at, errs);
                return;
            }
            error_ctx_error_d(errs, at, 2354,
                "This syntax requires an imported helper but module 'tslib' cannot be found.");
        }
        return;
    }
    // Each helper is looked for once per file (tsgo's sourceFileLinks.requestedExternalEmitHelpers; tsc 6 had it once
    // per helpers module). (The key differs from the not-found one above: a module was found)
    u64 key[3] = { KEY_EMIT_HELPERS, (u64)(uintptr_t)file, 1 };
    void const** slot = intern_slot(me, &my links, key, 3);
    u32 requested = (u32)(uintptr_t)*slot;
    u32 unchecked = helpers & ~requested;
    *slot = (void const*)(uintptr_t)(requested | helpers | EMIT_HELPER_REQUESTED);
    if (!unchecked) return;
    bool legacy = compiler_options()->experimental_decorators;
    for (u32 bit = 0; bit < c_arraylen(helper_names); bit++) {
        u32 helper = 1u << bit;
        if (!(unchecked & helper)) continue;
        const char* names[2] = { helper_names[bit][0], helper_names[bit][1] };
        if (helper == EMIT_HELPER_DECORATE && !legacy) {
            names[0] = "__esDecorate";
            names[1] = "__runInitializers";
        }
        for (u32 n = 0; n < 2 && names[n]; n++) {
            Symbol* symbol = symbol_get_export(module, csview_from(names[n]));
            if (symbol && symbol->flags & SYMBOL_ALIAS) symbol = symbol_resolve_alias(symbol);
            Error* error = nullptr;
            if (!symbol || !(symbol->flags & SYMBOL_VALUE)) {
                error = error_ctx_error_d(errs, at, 2343,
                    "This syntax requires an imported helper named '", names[n],
                    "' which does not exist in 'tslib'. Consider upgrading your version of 'tslib'.");
            } else if (helper_min_params(helper) && !has_signature_with_more_params(me, symbol, helper_min_params(helper))) {
                error = error_ctx_error_d(errs, at, 2807,
                    "This syntax requires an imported helper named '", names[n], "' with ",
                    helper_min_params(helper) + 1, " parameters, which is not compatible with the one in 'tslib'. "
                    "Consider upgrading your version of 'tslib'.");
            }
            // (Every checker asks: the program reports it once)
            if (error) {
                error->once_key = ((u64)(uintptr_t)file << 8) ^ (bit * 2 + n + 1);
                error->once_order = my file_order;
            }
        }
    }
}

bool ty_emits_commonjs_like(SourceFile const* file) {
    return source_file_emit_module_kind(file) < MODULE_KIND_SYSTEM;
}
