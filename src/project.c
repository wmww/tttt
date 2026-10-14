#include "project.h"

#include <stdlib.h>

typedef u32 ProjectIndex;
#define g_pro_key SvStr, svstr
#define g_val ProjectIndex, project_index
#include <cleat/generic/hmap.h>

typedef struct ProjectSet {
    Fs const* fs;
    Project* projects; // Boxed one by one: a Project never moves
    u32 count;
    u32 cap;
    HMapProjectIndex by_config;
    bool run_external_code;
} ProjectSet;

void project_set_run_external_code(ProjectSet* me, bool on) {
    my run_external_code = on;
}

ProjectSet* project_set_new(Fs const* fs) {
    return c_new(ProjectSet, { .fs = fs });
}

void project_set_destroy(ProjectSet* me) {
    if (!me) return;
    for (u32 i = 0; i < my count; i++) {
        Project* project = &my projects[i];
        cstr_drop(&project->config);
        compiler_options_drop(&project->options);
        str_list_drop(&project->root_files);
        tsconfig_references_drop(project->references, project->reference_count);
        free(project->referenced);
    }
    free(my projects);
    hmap_project_index_drop(&my by_config);
    free(me);
}

Project const* project_set_at(ProjectSet const* me, u32 index) {
    check(index < my count);
    return &my projects[index];
}

StrList const* project_set_root_files(ProjectSet* me, u32 index) {
    check(index < my count);
    Project* project = &my projects[index];
    if (!project->root_files_listed && project->readable) {
        CompilerOptions scratch = compiler_options_init();
        scratch.run_external_code = my run_external_code;
        try (tsconfig_load(my fs, cstr_sv(&project->config), &scratch, &project->root_files)) if_err (e) {
            ignore e;
            current_error_destroy();
        }
        compiler_options_drop(&scratch);
    }
    project->root_files_listed = true;
    return &project->root_files;
}

u32 project_set_load(ProjectSet* me, csview path) {
    cstr cwd = cstr_null;
    try (fs_get_cwd(my fs, &cwd)) or_panic;
    cstr config = absolute_path_of(cstr_sv(&cwd), path);
    cstr_drop(&cwd);
    if (fs_is_directory(my fs, cstr_sv(&config))) cstr_append_lit(&config, "/tsconfig.json");
    auto found = hmap_project_index_get(&my by_config, cstr_sv(&config));
    if (found) {
        cstr_drop(&config);
        return found->second;
    }
    if (my count == my cap) {
        my cap = my cap ? my cap * 2 : 8;
        my projects = realloc(my projects, sizeof(Project) * my cap);
    }
    u32 index = my count++;
    hmap_project_index_insert(&my by_config, cstr_clone(config), index);
    Project project = { .config = config, .options = compiler_options_init() };
    project.readable = true;
    try (tsconfig_load(my fs, cstr_sv(&project.config), &project.options, nullptr)) if_err (e) {
        ignore e;
        current_error_destroy();
        project.readable = false;
    }
    compiler_options_resolve(&project.options);
    if (project.readable) {
        try (tsconfig_load_references(my fs, cstr_sv(&project.config), &project.references, &project.reference_count,
            &project.has_references))
            if_err (e) {
            ignore e;
            current_error_destroy();
        }
    }
    project.referenced = c_new_n(u32, project.reference_count + 1);
    my projects[index] = project;
    // (my projects moves as it grows: index it again after each load)
    for (u32 i = 0; i < project.reference_count; i++) {
        u32 referenced = project_set_load(me, cstr_sv(&my projects[index].references[i].path));
        my projects[index].referenced[i] = referenced;
    }
    return index;
}

typedef struct BuildOrder {
    ProjectSet* set;
    uint8_t* marks; // 1: on the stack, 2: done
    u32* stack;
    u32 depth;
    u32* order;
    u32 count;
    StrList* cycle_errors;
} BuildOrder;

static void build_order_visit(BuildOrder* me, u32 project, bool in_circular_context) {
    if (my marks[project] == 2) return;
    if (my marks[project] == 1) {
        if (!in_circular_context) {
            // (tsc joins the stack with "\r\n")
            cstr message = cstr_lit("Project references may not form a circular graph. Cycle detected: ");
            for (u32 i = 0; i < my depth; i++) {
                if (i) cstr_append_lit(&message, "\r\n");
                cstr_append_sv(&message, cstr_sv(&my set->projects[my stack[i]].config));
            }
            str_list_push(my cycle_errors, cstr_sv(&message));
            cstr_drop(&message);
        }
        return;
    }
    my marks[project] = 1;
    my stack[my depth++] = project;
    for (u32 i = 0; i < my set->projects[project].reference_count; i++) {
        build_order_visit(
            me, my set->projects[project].referenced[i],
            in_circular_context || my set->projects[project].references[i].circular
        );
    }
    my depth--;
    my marks[project] = 2;
    my order[my count++] = project;
}

u32 project_set_build_order(ProjectSet* me, u32 const* roots, u32 root_count, u32** out, StrList* cycle_errors) {
    BuildOrder order = {
        .set = me,
        .marks = calloc((size_t)(my count + 1), 1),
        .stack = c_new_n(u32, my count + 1),
        .order = c_new_n(u32, my count + 1),
        .cycle_errors = cycle_errors,
    };
    for (u32 i = 0; i < root_count; i++) build_order_visit(&order, roots[i], false);
    free(order.marks);
    free(order.stack);
    *out = order.order;
    return order.count;
}

// ---- Redirects ----

typedef struct ProjectRedirects {
    ProjectSet* set;
    u32* projects; // The referenced projects (indices into set)
    u32 count;
    HMapProjectIndex source_projects; // A root file of a referenced project -> 1 + its index in projects
    HMapProjectIndex outputs; // An output declaration file -> its index in sources
    StrList sources;
    HMapProjectIndex source_outputs; // A source -> its index in output_paths
    StrList output_paths;
    StrList declaration_dirs;
} ProjectRedirects;

static bool is_declaration_name(csview path) {
    if (csview_ends_with(path, ".d.ts") || csview_ends_with(path, ".d.mts") || csview_ends_with(path, ".d.cts")) {
        return true;
    }
    // `x.d.css.ts` (allowArbitraryExtensions)
    csview base = basename_of(path);
    return csview_ends_with(base, ".ts") && csview_find_sv(base, c_sv(".d.")) != c_NPOS;
}

// tsc's getDeclarationEmitExtensionForPath, null for a file without a declaration output
static char const* declaration_extension(csview path, isize* stem) {
    static const struct { char const* from; char const* to; } map[] = {
        { ".mts", ".d.mts" }, { ".mjs", ".d.mts" }, { ".cts", ".d.cts" }, { ".cjs", ".d.cts" },
        { ".ts", ".d.ts" }, { ".tsx", ".d.ts" }, { ".js", ".d.ts" }, { ".jsx", ".d.ts" },
    };
    if (is_declaration_name(path) || csview_ends_with(path, ".json")) return nullptr;
    for (isize i = 0; i < c_countof(map); i++) {
        if (csview_ends_with(path, map[i].from)) {
            *stem = path.size - (isize)strlen(map[i].from);
            return map[i].to;
        }
    }
    return nullptr;
}

static void redirects_add_project(ProjectRedirects* me, u32 project_index) {
    project_set_root_files(my set, project_index);
    Project const* project = &my set->projects[project_index];
    u32 slot = my count++;
    my projects[slot] = project_index;
    CompilerOptions const* o = &project->options;
    csview declaration_dir = cstr_size(&o->declaration_dir) ? cstr_sv(&o->declaration_dir) : cstr_sv(&o->out_dir);
    if (declaration_dir.size) str_list_push(&my declaration_dirs, declaration_dir);
    cstr common = cstr_null;
    for (isize i = 0; i < project->root_files.count; i++) {
        csview file = cstr_sv(&project->root_files.items[i]);
        // (A file of several referenced projects is the last one's, children after parents: tsgo's initMapperWorker
        // maps.Copy overwrites)
        hmap_project_index_insert_or_assign(&my source_projects, cstr_from_sv(file), slot + 1);
        isize stem = 0;
        char const* extension = declaration_extension(file, &stem);
        if (!extension) continue;
        cstr output = cstr_null;
        if (declaration_dir.size) {
            if (!cstr_size(&common)) {
                // (tsc's getCommonSourceDirectoryOfConfig: a project never infers it from its files since 6.0)
                common = cstr_size(&o->root_dir) ? cstr_clone(o->root_dir) : cstr_from_sv(dirname_of(cstr_sv(&project->config)));
            }
            // (Outside the root directory it goes up with `..`, and the program says TS6059)
            cstr relative = relative_path_of(cstr_sv(&common), csview_slice(file, 0, stem));
            output = cstr_d(declaration_dir, "/", relative, extension);
            cstr_drop(&relative);
            cstr normal = absolute_path_of(c_sv("/"), cstr_sv(&output));
            cstr_take(&output, normal);
        } else {
            output = cstr_d(csview_slice(file, 0, stem), extension);
        }
        u32 source = (u32)my sources.count;
        str_list_push(&my sources, file);
        hmap_project_index_insert_or_assign(&my source_outputs, cstr_from_sv(file), (u32)my output_paths.count);
        str_list_push(&my output_paths, cstr_sv(&output));
        hmap_project_index_insert_or_assign(&my outputs, output, source);
    }
    cstr_drop(&common);
}

static void redirects_collect(ProjectRedirects* me, u32 project, uint8_t* seen) {
    Project const* p = &my set->projects[project];
    for (u32 i = 0; i < p->reference_count; i++) {
        u32 referenced = p->referenced[i];
        if (seen[referenced]) continue;
        seen[referenced] = 1;
        if (my set->projects[referenced].readable) redirects_add_project(me, referenced);
        redirects_collect(me, referenced, seen);
    }
}

ProjectRedirects* project_redirects_new(ProjectSet* set, u32 project) {
    ProjectRedirects* me = c_new(ProjectRedirects, { .set = set });
    my projects = c_new_n(u32, set->count + 1);
    uint8_t* seen = calloc((size_t)(set->count + 1), 1);
    seen[project] = 1;
    redirects_collect(me, project, seen);
    free(seen);
    if (getenv("TTTT_DEBUG_PROJECTS")) {
        eprintln("redirects of ", set->projects[project].config, " (", set->projects[project].reference_count,
            " references): ", my count, " projects, ", my sources.count, " outputs");
        for (u32 i = 0; i < my count; i++) {
            Project const* p = &set->projects[my projects[i]];
            eprintln("  ", p->config, ": ", p->root_files.count, " files",
                p->root_files.count ? ", first " : "", p->root_files.count ? cstr_sv(&p->root_files.items[0]) : c_sv(""));
        }
        for (isize i = 0; i < my declaration_dirs.count; i++) eprintln("  declaration dir ", my declaration_dirs.items[i]);
    }
    return me;
}

void project_redirects_destroy(ProjectRedirects* me) {
    if (!me) return;
    free(my projects);
    hmap_project_index_drop(&my source_projects);
    hmap_project_index_drop(&my outputs);
    str_list_drop(&my sources);
    hmap_project_index_drop(&my source_outputs);
    str_list_drop(&my output_paths);
    str_list_drop(&my declaration_dirs);
    free(me);
}

u32 project_redirects_count(ProjectRedirects const* me) {
    return me ? my count : 0;
}

CompilerOptions const* project_redirects_options(ProjectRedirects const* me, u32 index) {
    check(index < my count);
    return &my set->projects[my projects[index]].options;
}

u32 project_redirects_project_of(ProjectRedirects const* me, csview absolute) {
    if (!me) return 0;
    auto found = hmap_project_index_get(&my source_projects, absolute);
    return found ? found->second : 0;
}

csview project_redirects_source_of_output(ProjectRedirects const* me, csview absolute) {
    if (!me) return c_sv("");
    auto found = hmap_project_index_get(&my outputs, absolute);
    return found ? cstr_sv(&my sources.items[found->second]) : c_sv("");
}

csview project_redirects_output_of_source(ProjectRedirects const* me, csview absolute) {
    if (!me) return c_sv("");
    auto found = hmap_project_index_get(&my source_outputs, absolute);
    return found ? cstr_sv(&my output_paths.items[found->second]) : c_sv("");
}

bool project_redirects_output_directory(ProjectRedirects const* me, csview absolute) {
    if (!me) return false;
    for (isize i = 0; i < my declaration_dirs.count; i++) {
        csview dir = cstr_sv(&my declaration_dirs.items[i]);
        csview shorter = absolute.size <= dir.size ? absolute : dir;
        csview longer = absolute.size <= dir.size ? dir : absolute;
        // Equal, one inside the other
        if (memcmp(shorter.buf, longer.buf, (size_t)shorter.size)) continue;
        if (shorter.size == longer.size || longer.buf[shorter.size] == '/' || (shorter.size == 1 && shorter.buf[0] == '/')) {
            return true;
        }
    }
    return false;
}
