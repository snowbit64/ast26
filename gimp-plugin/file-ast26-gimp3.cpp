// SPDX-License-Identifier: MIT
// GIMP 3.0 file-format plug-in for Farming Simulator 2026 AST textures.
// Links against libgimp-3.0 and the ast26 static core library.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "ast26/ast26.hpp"

#define PLUG_IN_BINARY  "file-ast26"
#define LOAD_PROC       "file-ast26-load"
#define EXPORT_PROC     "file-ast26-export"

// ── Helpers ──────────────────────────────────────────────────────────

static std::vector<std::uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return {std::istreambuf_iterator<char>(f),
            std::istreambuf_iterator<char>()};
}

static bool write_file(const char* path, const std::vector<std::uint8_t>& d) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(d.data()),
            static_cast<std::streamsize>(d.size()));
    return f.good();
}

// ── Block-size tables ────────────────────────────────────────────────

static const int block_vals[][2] = {
    {4,4},{5,4},{5,5},{6,5},{6,6},{8,5},{8,6},{8,8},
    {10,5},{10,6},{10,8},{10,10},{12,10},{12,12}
};
static const int N_BLOCKS = 14;

static int find_block_index(int bx, int by) {
    for (int i = 0; i < N_BLOCKS; ++i)
        if (block_vals[i][0] == bx && block_vals[i][1] == by) return i;
    return 4; // default 6x6
}

// ── GObject plug-in type ─────────────────────────────────────────────

typedef struct _FileAst26      FileAst26;
typedef struct _FileAst26Class FileAst26Class;

struct _FileAst26 {
    GimpPlugIn parent_instance;
};

struct _FileAst26Class {
    GimpPlugInClass parent_class;
};

#define FILE_AST26_TYPE (file_ast26_get_type())
#define FILE_AST26(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), FILE_AST26_TYPE, FileAst26))

GType file_ast26_get_type(void) G_GNUC_CONST;

static GList*          file_ast26_query_procedures(GimpPlugIn  *plug_in);
static GimpProcedure*  file_ast26_create_procedure(GimpPlugIn  *plug_in,
                                                    const gchar *name);

static GimpValueArray* ast26_load(GimpProcedure         *procedure,
                                  GimpRunMode            run_mode,
                                  GFile                 *file,
                                  GimpMetadata          *metadata,
                                  GimpMetadataLoadFlags *flags,
                                  GimpProcedureConfig   *config,
                                  gpointer               run_data);

static GimpValueArray* ast26_export(GimpProcedure        *procedure,
                                    GimpRunMode           run_mode,
                                    GimpImage            *image,
                                    GFile                *file,
                                    GimpExportOptions    *options,
                                    GimpMetadata         *metadata,
                                    GimpProcedureConfig  *config,
                                    gpointer              run_data);

G_DEFINE_TYPE(FileAst26, file_ast26, GIMP_TYPE_PLUG_IN)

GIMP_MAIN(FILE_AST26_TYPE)

static void
file_ast26_class_init(FileAst26Class *klass) {
    GimpPlugInClass *plug_in_class = GIMP_PLUG_IN_CLASS(klass);
    plug_in_class->query_procedures = file_ast26_query_procedures;
    plug_in_class->create_procedure = file_ast26_create_procedure;
}

static void
file_ast26_init(FileAst26 *) {
}

// ── Procedure list ──────────────────────────────────────────────────

static GList*
file_ast26_query_procedures(GimpPlugIn *) {
    GList *list = NULL;
    list = g_list_append(list, g_strdup(LOAD_PROC));
    list = g_list_append(list, g_strdup(EXPORT_PROC));
    return list;
}

// ── Procedure factory ───────────────────────────────────────────────

static GimpProcedure*
file_ast26_create_procedure(GimpPlugIn  *plug_in,
                             const gchar *name) {
    GimpProcedure *procedure = NULL;

    if (!std::strcmp(name, LOAD_PROC)) {
        procedure = gimp_load_procedure_new(plug_in, name,
                                            GIMP_PDB_PROC_TYPE_PLUGIN,
                                            ast26_load, NULL, NULL);

        gimp_procedure_set_menu_label(procedure,
                                      "Farming Simulator 2026 AST");
        gimp_procedure_set_documentation(
            procedure,
            "Load Farming Simulator 2026 AST texture",
            "Loads .ast (GS2D v8) texture files used by "
            "Farming Simulator 2026. Supports ASTC-compressed "
            "and uncompressed textures with multiple layers "
            "and mipmap levels.",
            name);
        gimp_procedure_set_attribution(procedure,
                                       "ast26 contributors",
                                       "MIT License",
                                       "2026");

        gimp_file_procedure_set_mime_types(GIMP_FILE_PROCEDURE(procedure),
                                           "image/x-ast26");
        gimp_file_procedure_set_extensions(GIMP_FILE_PROCEDURE(procedure),
                                           "ast");

    } else if (!std::strcmp(name, EXPORT_PROC)) {
        procedure = gimp_export_procedure_new(plug_in, name,
                                              GIMP_PDB_PROC_TYPE_PLUGIN,
                                              FALSE,
                                              ast26_export, NULL, NULL);

        gimp_procedure_set_image_types(procedure, "RGB*, GRAY*");
        gimp_procedure_set_menu_label(procedure,
                                      "Farming Simulator 2026 AST");
        gimp_file_procedure_set_format_name(GIMP_FILE_PROCEDURE(procedure),
                                            "AST");
        gimp_procedure_set_documentation(
            procedure,
            "Export as Farming Simulator 2026 AST texture",
            "Exports images as .ast (GS2D v8) texture files for "
            "Farming Simulator 2026. ASTC compression with "
            "configurable block size, quality, and automatic "
            "mipmap generation.",
            name);
        gimp_procedure_set_attribution(procedure,
                                       "ast26 contributors",
                                       "MIT License",
                                       "2026");

        gimp_file_procedure_set_mime_types(GIMP_FILE_PROCEDURE(procedure),
                                           "image/x-ast26");
        gimp_file_procedure_set_extensions(GIMP_FILE_PROCEDURE(procedure),
                                           "ast");

        gimp_export_procedure_set_capabilities(
            GIMP_EXPORT_PROCEDURE(procedure),
            static_cast<GimpExportCapabilities>(
                GIMP_EXPORT_CAN_HANDLE_RGB |
                GIMP_EXPORT_CAN_HANDLE_ALPHA),
            NULL, NULL, NULL);

        gimp_procedure_add_choice_argument(
            procedure, "block-size",
            "ASTC Block Size",
            "ASTC compression block size",
            gimp_choice_new_with_values(
                "4x4",   0,  "4×4",   NULL,
                "5x4",   1,  "5×4",   NULL,
                "5x5",   2,  "5×5",   NULL,
                "6x5",   3,  "6×5",   NULL,
                "6x6",   4,  "6×6",   NULL,
                "8x5",   5,  "8×5",   NULL,
                "8x6",   6,  "8×6",   NULL,
                "8x8",   7,  "8×8",   NULL,
                "10x5",  8,  "10×5",  NULL,
                "10x6",  9,  "10×6",  NULL,
                "10x8",  10, "10×8",  NULL,
                "10x10", 11, "10×10", NULL,
                "12x10", 12, "12×10", NULL,
                "12x12", 13, "12×12", NULL,
                NULL),
            "6x6",
            G_PARAM_READWRITE);

        gimp_procedure_add_choice_argument(
            procedure, "quality",
            "Compression Quality",
            "ASTC compression quality level",
            gimp_choice_new_with_values(
                "fast",     0, "Fast",     NULL,
                "medium",   1, "Medium",   NULL,
                "thorough", 2, "Thorough", NULL,
                NULL),
            "fast",
            G_PARAM_READWRITE);

        gimp_procedure_add_int_argument(
            procedure, "mipmaps",
            "Mipmaps",
            "Number of mipmap levels to generate (-1 = all)",
            -1, 16, -1,
            G_PARAM_READWRITE);

        gimp_procedure_add_choice_argument(
            procedure, "color-space",
            "Color Space",
            "Color space for ASTC encoding",
            gimp_choice_new_with_values(
                "srgb",   0, "sRGB",   NULL,
                "linear", 1, "Linear", NULL,
                "alpha",  2, "Alpha",  NULL,
                NULL),
            "srgb",
            G_PARAM_READWRITE);
    }

    return procedure;
}

// ── Load .ast → GIMP image ──────────────────────────────────────────

static GimpValueArray*
ast26_load(GimpProcedure         *procedure,
           GimpRunMode            run_mode,
           GFile                 *file,
           GimpMetadata          *,
           GimpMetadataLoadFlags *,
           GimpProcedureConfig   *,
           gpointer) {
    gchar *path = g_file_get_path(file);
    if (!path)
        return gimp_procedure_new_return_values(procedure,
                                                GIMP_PDB_EXECUTION_ERROR,
                                                NULL);

    auto data = read_file(path);
    if (data.empty()) {
        g_message("ast26: cannot read '%s'", path);
        g_free(path);
        return gimp_procedure_new_return_values(procedure,
                                                GIMP_PDB_EXECUTION_ERROR,
                                                NULL);
    }

    ast26::Image img;
    try {
        img = ast26::decode(data.data(), data.size());
    } catch (const std::exception& e) {
        g_message("ast26 decode error: %s", e.what());
        g_free(path);
        return gimp_procedure_new_return_values(procedure,
                                                GIMP_PDB_EXECUTION_ERROR,
                                                NULL);
    }

    const int w = img.width;
    const int h = img.height;
    if (w <= 0 || h <= 0 || img.layers.empty() || img.layers[0].empty()) {
        g_message("ast26: decoded image is empty");
        g_free(path);
        return gimp_procedure_new_return_values(procedure,
                                                GIMP_PDB_EXECUTION_ERROR,
                                                NULL);
    }

    const auto& rgba = img.layers[0][0].data;
    if (rgba.size() < static_cast<std::size_t>(w) * h * 4) {
        g_message("ast26: incomplete pixel data");
        g_free(path);
        return gimp_procedure_new_return_values(procedure,
                                                GIMP_PDB_EXECUTION_ERROR,
                                                NULL);
    }

    gegl_init(NULL, NULL);

    GimpImage *image = gimp_image_new(w, h, GIMP_RGB);
    gimp_image_set_file(image, file);

    const Babl *format = babl_format("R'G'B'A u8");

    // Base layer (mip 0, layer 0)
    GimpLayer *layer = gimp_layer_new(image, "Background",
                                      w, h, GIMP_RGBA_IMAGE, 100.0,
                                      GIMP_LAYER_MODE_NORMAL);
    gimp_image_insert_layer(image, layer, NULL, 0);

    GeglBuffer *buffer = gimp_drawable_get_buffer(GIMP_DRAWABLE(layer));
    gegl_buffer_set(buffer, GEGL_RECTANGLE(0, 0, w, h), 0,
                    format, rgba.data(), GEGL_AUTO_ROWSTRIDE);
    g_object_unref(buffer);

    // Additional array layers
    for (std::size_t L = 1; L < img.layers.size(); ++L) {
        if (img.layers[L].empty()) continue;
        const auto& mip0 = img.layers[L][0];
        if (mip0.data.size() < static_cast<std::size_t>(mip0.width) * mip0.height * 4)
            continue;

        char name[64];
        g_snprintf(name, sizeof(name), "Layer %zu", L);
        GimpLayer *lid = gimp_layer_new(image, name,
                                        mip0.width, mip0.height,
                                        GIMP_RGBA_IMAGE, 100.0,
                                        GIMP_LAYER_MODE_NORMAL);
        gimp_image_insert_layer(image, lid, NULL, -1);

        GeglBuffer *buf = gimp_drawable_get_buffer(GIMP_DRAWABLE(lid));
        gegl_buffer_set(buf, GEGL_RECTANGLE(0, 0, mip0.width, mip0.height), 0,
                        format, mip0.data.data(), GEGL_AUTO_ROWSTRIDE);
        g_object_unref(buf);
    }

    // Store original block-size as a parasite
    if (img.compression.x > 0 && img.compression.y > 0) {
        char meta[64];
        g_snprintf(meta, sizeof(meta), "%dx%d",
                   img.compression.x, img.compression.y);
        GimpParasite *p = gimp_parasite_new("ast26-block-size",
                                            GIMP_PARASITE_PERSISTENT,
                                            static_cast<guint32>(strlen(meta) + 1),
                                            meta);
        gimp_image_attach_parasite(image, p);
        gimp_parasite_free(p);
    }

    gimp_displays_flush();
    g_free(path);

    GimpValueArray *return_vals =
        gimp_procedure_new_return_values(procedure, GIMP_PDB_SUCCESS, NULL);
    GIMP_VALUES_SET_IMAGE(return_vals, 1, image);
    return return_vals;
}

// ── Export GIMP image → .ast ────────────────────────────────────────

static GimpValueArray*
ast26_export(GimpProcedure        *procedure,
             GimpRunMode           run_mode,
             GimpImage            *image,
             GFile                *file,
             GimpExportOptions    *options,
             GimpMetadata         *,
             GimpProcedureConfig  *config,
             gpointer) {
    gegl_init(NULL, NULL);

    // Read block-size parasite and set as default if present
    GimpParasite *parasite = gimp_image_get_parasite(image,
                                                     "ast26-block-size");
    if (parasite) {
        int bx = 0, by = 0;
        guint32 psize = 0;
        if (sscanf(static_cast<const char*>(gimp_parasite_get_data(parasite, &psize)),
                   "%dx%d", &bx, &by) == 2 && bx > 0 && by > 0) {
            int idx = find_block_index(bx, by);
            const char *block_labels[] = {
                "4x4","5x4","5x5","6x5","6x6","8x5","8x6","8x8",
                "10x5","10x6","10x8","10x10","12x10","12x12"
            };
            if (idx >= 0 && idx < N_BLOCKS)
                g_object_set(config, "block-size", block_labels[idx], NULL);
        }
        gimp_parasite_free(parasite);
    }

    if (run_mode == GIMP_RUN_INTERACTIVE) {
        gimp_ui_init(PLUG_IN_BINARY);

        GtkWidget *dialog = gimp_export_procedure_dialog_new(
            GIMP_EXPORT_PROCEDURE(procedure),
            GIMP_PROCEDURE_CONFIG(config),
            image);

        GtkWidget *vbox = gimp_procedure_dialog_fill_box(
            GIMP_PROCEDURE_DIALOG(dialog),
            "ast-export-vbox",
            "block-size", "quality", "mipmaps", "color-space",
            NULL);
        gtk_box_set_spacing(GTK_BOX(vbox), 12);

        gimp_procedure_dialog_fill(GIMP_PROCEDURE_DIALOG(dialog),
                                   "ast-export-vbox",
                                   NULL);

        gboolean run = gimp_procedure_dialog_run(
            GIMP_PROCEDURE_DIALOG(dialog));
        gtk_widget_destroy(dialog);

        if (!run)
            return gimp_procedure_new_return_values(procedure,
                                                    GIMP_PDB_CANCEL,
                                                    NULL);
    }

    // Apply export options (flatten, convert etc.)
    GimpExportReturn export_ret = gimp_export_options_get_image(options,
                                                                &image);

    GList *drawables = gimp_image_list_layers(image);

    gint w = gimp_image_get_width(image);
    gint h = gimp_image_get_height(image);

    // Flatten and read pixels
    GimpLayer *merged = gimp_image_flatten(image);
    GeglBuffer *gbuf = gimp_drawable_get_buffer(GIMP_DRAWABLE(merged));
    const Babl *format = babl_format("R'G'B'A u8");

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
    gegl_buffer_get(gbuf, GEGL_RECTANGLE(0, 0, w, h), 1.0,
                    format, pixels.data(), GEGL_AUTO_ROWSTRIDE,
                    GEGL_ABYSS_NONE);
    g_object_unref(gbuf);

    // Read export parameters from config
    gchar *block_str = NULL;
    gchar *quality_str = NULL;
    gint   mipmaps = -1;
    gchar *colorspace_str = NULL;

    g_object_get(config,
                 "block-size",   &block_str,
                 "quality",      &quality_str,
                 "mipmaps",      &mipmaps,
                 "color-space",  &colorspace_str,
                 NULL);

    int block_x = 6, block_y = 6;
    if (block_str) {
        for (int i = 0; i < N_BLOCKS; ++i) {
            char label[8];
            g_snprintf(label, sizeof(label), "%dx%d",
                       block_vals[i][0], block_vals[i][1]);
            if (std::strcmp(block_str, label) == 0) {
                block_x = block_vals[i][0];
                block_y = block_vals[i][1];
                break;
            }
        }
        g_free(block_str);
    }

    ast26::Quality q = ast26::Quality::Fast;
    if (quality_str) {
        if (std::strcmp(quality_str, "medium") == 0)
            q = ast26::Quality::Medium;
        else if (std::strcmp(quality_str, "thorough") == 0)
            q = ast26::Quality::Thorough;
        g_free(quality_str);
    }

    ast26::ColorSpace cs = ast26::ColorSpace::Srgb;
    if (colorspace_str) {
        if (std::strcmp(colorspace_str, "linear") == 0)
            cs = ast26::ColorSpace::Linear;
        else if (std::strcmp(colorspace_str, "alpha") == 0)
            cs = ast26::ColorSpace::Alpha;
        g_free(colorspace_str);
    }

    // Build ast26 Image
    ast26::Image src;
    src.width  = w;
    src.height = h;
    src.color_format = ast26::ColorFormat::RGBA32;
    src.layers = {{ast26::MipLevel{w, h, std::move(pixels)}}};

    ast26::EncodeOptions opts;
    opts.block_size  = {block_x, block_y};
    opts.mipmaps     = mipmaps;
    opts.quality     = q;
    opts.color_space = cs;

    std::vector<std::uint8_t> ast_data;
    try {
        ast_data = ast26::encode(src, opts);
    } catch (const std::exception& e) {
        g_message("ast26 encode error: %s", e.what());
        if (export_ret == GIMP_EXPORT_EXPORT)
            gimp_image_delete(image);
        g_list_free(drawables);
        return gimp_procedure_new_return_values(procedure,
                                                GIMP_PDB_EXECUTION_ERROR,
                                                NULL);
    }

    gchar *filepath = g_file_get_path(file);
    gboolean ok = write_file(filepath, ast_data);
    g_free(filepath);

    if (export_ret == GIMP_EXPORT_EXPORT)
        gimp_image_delete(image);
    g_list_free(drawables);

    if (!ok) {
        g_message("ast26: cannot write file");
        return gimp_procedure_new_return_values(procedure,
                                                GIMP_PDB_EXECUTION_ERROR,
                                                NULL);
    }

    return gimp_procedure_new_return_values(procedure,
                                            GIMP_PDB_SUCCESS,
                                            NULL);
}
