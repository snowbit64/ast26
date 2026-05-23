// SPDX-License-Identifier: MIT
// GIMP 2.10 file-format plug-in for Farming Simulator 2026 AST textures.
// Links against libgimp-2.0 and the ast26 static core library.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "ast26/ast26.hpp"

// ── Plug-in identifiers ──────────────────────────────────────────────
#define PLUG_IN_BINARY  "file-ast26"
#define LOAD_PROC       "file-ast26-load"
#define SAVE_PROC       "file-ast26-save"

// ── Persistent encode settings ───────────────────────────────────────
struct SaveSettings {
    gint block_x   = 6;
    gint block_y   = 6;
    gint quality   = 0;   // 0=fast 1=medium 2=thorough
    gint mipmaps   = -1;  // -1 = max
    gint colorspace = 0;  // 0=srgb 1=linear 2=alpha
};

static SaveSettings save_cfg;

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

// ── Load .ast → GIMP image ──────────────────────────────────────────

static gint32 load_ast(const gchar* filename) {
    auto data = read_file(filename);
    if (data.empty()) {
        g_message("ast26: cannot read '%s'", filename);
        return -1;
    }

    ast26::Image img;
    try {
        img = ast26::decode(data.data(), data.size());
    } catch (const std::exception& e) {
        g_message("ast26 decode error: %s", e.what());
        return -1;
    }

    const int w = img.width;
    const int h = img.height;
    if (w <= 0 || h <= 0 || img.layers.empty() || img.layers[0].empty()) {
        g_message("ast26: decoded image is empty");
        return -1;
    }

    const auto& rgba = img.layers[0][0].data;
    if (rgba.size() < static_cast<std::size_t>(w) * h * 4) {
        g_message("ast26: incomplete pixel data");
        return -1;
    }

    gint32 image_id = gimp_image_new(w, h, GIMP_RGB);
    gimp_image_set_filename(image_id, filename);

    // Import base layer (mip 0, layer 0)
    gint32 layer_id = gimp_layer_new(image_id, "Background",
                                     w, h, GIMP_RGBA_IMAGE, 100.0,
                                     GIMP_LAYER_MODE_NORMAL);
    gimp_image_insert_layer(image_id, layer_id, -1, 0);

    GeglBuffer* buffer = gimp_drawable_get_buffer(layer_id);
    const Babl* format = babl_format("R'G'B'A u8");
    gegl_buffer_set(buffer, GEGL_RECTANGLE(0, 0, w, h), 0,
                    format, rgba.data(), GEGL_AUTO_ROWSTRIDE);
    g_object_unref(buffer);

    gimp_image_set_active_layer(image_id, layer_id);

    // Import additional array layers (if present)
    for (std::size_t L = 1; L < img.layers.size(); ++L) {
        if (img.layers[L].empty()) continue;
        const auto& mip0 = img.layers[L][0];
        if (mip0.data.size() < static_cast<std::size_t>(mip0.width) * mip0.height * 4)
            continue;

        char name[64];
        g_snprintf(name, sizeof(name), "Layer %zu", L);
        gint32 lid = gimp_layer_new(image_id, name,
                                    mip0.width, mip0.height,
                                    GIMP_RGBA_IMAGE, 100.0,
                                    GIMP_LAYER_MODE_NORMAL);
        gimp_image_insert_layer(image_id, lid, -1, -1);

        GeglBuffer* buf = gimp_drawable_get_buffer(lid);
        gegl_buffer_set(buf, GEGL_RECTANGLE(0, 0, mip0.width, mip0.height), 0,
                        format, mip0.data.data(), GEGL_AUTO_ROWSTRIDE);
        g_object_unref(buf);
    }

    // Store AST metadata as parasites
    if (img.compression.x > 0 && img.compression.y > 0) {
        char meta[64];
        g_snprintf(meta, sizeof(meta), "%dx%d", img.compression.x, img.compression.y);
        GimpParasite* p = gimp_parasite_new("ast26-block-size",
                                            GIMP_PARASITE_PERSISTENT,
                                            static_cast<guint32>(strlen(meta) + 1),
                                            meta);
        gimp_image_attach_parasite(image_id, p);
        gimp_parasite_free(p);
    }

    gimp_displays_flush();
    return image_id;
}

// ── Save dialog ─────────────────────────────────────────────────────

static const char* block_labels[] = {
    "4x4", "5x4", "5x5", "6x5", "6x6", "8x5", "8x6", "8x8",
    "10x5", "10x6", "10x8", "10x10", "12x10", "12x12", NULL
};
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

static gboolean save_dialog() {
    GtkWidget* dialog = gimp_export_dialog_new("AST (FS2026)",
                                               PLUG_IN_BINARY,
                                               SAVE_PROC);

    GtkWidget* vbox = gtk_vbox_new(FALSE, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    gtk_box_pack_start(GTK_BOX(gimp_export_dialog_get_content_area(dialog)),
                       vbox, TRUE, TRUE, 0);
    gtk_widget_show(vbox);

    // Block size
    GtkWidget* frame_block = gimp_frame_new("ASTC Block Size");
    gtk_box_pack_start(GTK_BOX(vbox), frame_block, FALSE, FALSE, 0);
    gtk_widget_show(frame_block);

    gint block_idx = find_block_index(save_cfg.block_x, save_cfg.block_y);
    GtkWidget* combo_block = gimp_int_combo_box_new_array(N_BLOCKS, block_labels);
    gimp_int_combo_box_set_active(GIMP_INT_COMBO_BOX(combo_block), block_idx);
    gtk_container_add(GTK_CONTAINER(frame_block), combo_block);
    gtk_widget_show(combo_block);

    // Quality
    GtkWidget* frame_q = gimp_frame_new("Compression Quality");
    gtk_box_pack_start(GTK_BOX(vbox), frame_q, FALSE, FALSE, 0);
    gtk_widget_show(frame_q);

    const char* q_labels[] = {"Fast", "Medium", "Thorough", NULL};
    GtkWidget* combo_q = gimp_int_combo_box_new_array(3, q_labels);
    gimp_int_combo_box_set_active(GIMP_INT_COMBO_BOX(combo_q), save_cfg.quality);
    gtk_container_add(GTK_CONTAINER(frame_q), combo_q);
    gtk_widget_show(combo_q);

    // Mipmaps
    GtkWidget* frame_mip = gimp_frame_new("Mipmaps");
    gtk_box_pack_start(GTK_BOX(vbox), frame_mip, FALSE, FALSE, 0);
    gtk_widget_show(frame_mip);

    GtkWidget* spin_mip = gimp_spin_button_new(
        GTK_ADJUSTMENT(gtk_adjustment_new(save_cfg.mipmaps, -1, 16, 1, 1, 0)),
        1.0, 0);
    gtk_widget_set_tooltip_text(spin_mip, "-1 = generate all mip levels");
    gtk_container_add(GTK_CONTAINER(frame_mip), spin_mip);
    gtk_widget_show(spin_mip);

    // Color space
    GtkWidget* frame_cs = gimp_frame_new("Color Space");
    gtk_box_pack_start(GTK_BOX(vbox), frame_cs, FALSE, FALSE, 0);
    gtk_widget_show(frame_cs);

    const char* cs_labels[] = {"sRGB", "Linear", "Alpha", NULL};
    GtkWidget* combo_cs = gimp_int_combo_box_new_array(3, cs_labels);
    gimp_int_combo_box_set_active(GIMP_INT_COMBO_BOX(combo_cs), save_cfg.colorspace);
    gtk_container_add(GTK_CONTAINER(frame_cs), combo_cs);
    gtk_widget_show(combo_cs);

    gboolean run = (gimp_dialog_run(GIMP_DIALOG(dialog)) == GTK_RESPONSE_OK);

    if (run) {
        gint bi = 0;
        gimp_int_combo_box_get_active(GIMP_INT_COMBO_BOX(combo_block), &bi);
        if (bi >= 0 && bi < N_BLOCKS) {
            save_cfg.block_x = block_vals[bi][0];
            save_cfg.block_y = block_vals[bi][1];
        }
        gimp_int_combo_box_get_active(GIMP_INT_COMBO_BOX(combo_q), &save_cfg.quality);
        save_cfg.mipmaps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_mip));
        gimp_int_combo_box_get_active(GIMP_INT_COMBO_BOX(combo_cs), &save_cfg.colorspace);
    }

    gtk_widget_destroy(dialog);
    return run;
}

// ── Save GIMP image → .ast ──────────────────────────────────────────

static GimpPDBStatusType save_ast(gint32 image_id,
                                  gint32 drawable_id,
                                  const gchar* filename) {
    gint w = gimp_image_width(image_id);
    gint h = gimp_image_height(image_id);

    // Flatten and get pixels
    gint32 merged = gimp_image_flatten(image_id);
    GeglBuffer* buffer = gimp_drawable_get_buffer(merged);
    const Babl* format = babl_format("R'G'B'A u8");

    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
    gegl_buffer_get(buffer, GEGL_RECTANGLE(0, 0, w, h), 1.0,
                    format, pixels.data(), GEGL_AUTO_ROWSTRIDE,
                    GEGL_ABYSS_NONE);
    g_object_unref(buffer);

    // Build ast26 Image
    ast26::Image src;
    src.width = w;
    src.height = h;
    src.color_format = ast26::ColorFormat::RGBA32;
    src.layers = {{ast26::MipLevel{w, h, std::move(pixels)}}};

    ast26::EncodeOptions opts;
    opts.block_size = {save_cfg.block_x, save_cfg.block_y};
    opts.mipmaps = save_cfg.mipmaps;

    switch (save_cfg.quality) {
        case 0: opts.quality = ast26::Quality::Fast; break;
        case 1: opts.quality = ast26::Quality::Medium; break;
        case 2: opts.quality = ast26::Quality::Thorough; break;
        default: opts.quality = ast26::Quality::Fast; break;
    }

    switch (save_cfg.colorspace) {
        case 0: opts.color_space = ast26::ColorSpace::Srgb; break;
        case 1: opts.color_space = ast26::ColorSpace::Linear; break;
        case 2: opts.color_space = ast26::ColorSpace::Alpha; break;
        default: opts.color_space = ast26::ColorSpace::Srgb; break;
    }

    std::vector<std::uint8_t> ast_data;
    try {
        ast_data = ast26::encode(src, opts);
    } catch (const std::exception& e) {
        g_message("ast26 encode error: %s", e.what());
        return GIMP_PDB_EXECUTION_ERROR;
    }

    if (!write_file(filename, ast_data)) {
        g_message("ast26: cannot write '%s'", filename);
        return GIMP_PDB_EXECUTION_ERROR;
    }

    return GIMP_PDB_SUCCESS;
}

// ── GIMP plug-in entry points ───────────────────────────────────────

static void query() {
    // Load procedure
    static const GimpParamDef load_args[] = {
        {GIMP_PDB_INT32,  const_cast<gchar*>("run-mode"),
         const_cast<gchar*>("Run mode")},
        {GIMP_PDB_STRING, const_cast<gchar*>("filename"),
         const_cast<gchar*>("File name")},
        {GIMP_PDB_STRING, const_cast<gchar*>("raw-filename"),
         const_cast<gchar*>("Raw file name")},
    };
    static const GimpParamDef load_return[] = {
        {GIMP_PDB_IMAGE, const_cast<gchar*>("image"),
         const_cast<gchar*>("Loaded image")},
    };

    gimp_install_procedure(
        LOAD_PROC,
        "Load Farming Simulator 2026 AST texture",
        "Loads .ast (GS2D v8) texture files used by Farming Simulator 2026. "
        "Supports ASTC-compressed and uncompressed textures with multiple "
        "layers and mipmap levels.",
        "ast26 contributors",
        "MIT License",
        "2026",
        "Farming Simulator 2026 AST",
        NULL,
        GIMP_PLUGIN,
        G_N_ELEMENTS(load_args), G_N_ELEMENTS(load_return),
        load_args, load_return);

    gimp_register_file_handler_mime(LOAD_PROC, "image/x-ast26");
    gimp_register_load_handler(LOAD_PROC, "ast", "");

    // Save procedure
    static const GimpParamDef save_args[] = {
        {GIMP_PDB_INT32,    const_cast<gchar*>("run-mode"),
         const_cast<gchar*>("Run mode")},
        {GIMP_PDB_IMAGE,    const_cast<gchar*>("image"),
         const_cast<gchar*>("Image to save")},
        {GIMP_PDB_DRAWABLE, const_cast<gchar*>("drawable"),
         const_cast<gchar*>("Active drawable")},
        {GIMP_PDB_STRING,   const_cast<gchar*>("filename"),
         const_cast<gchar*>("File name")},
        {GIMP_PDB_STRING,   const_cast<gchar*>("raw-filename"),
         const_cast<gchar*>("Raw file name")},
    };

    gimp_install_procedure(
        SAVE_PROC,
        "Save as Farming Simulator 2026 AST texture",
        "Exports images as .ast (GS2D v8) texture files for Farming Simulator "
        "2026. ASTC compression with configurable block size, quality, and "
        "automatic mipmap generation.",
        "ast26 contributors",
        "MIT License",
        "2026",
        "Farming Simulator 2026 AST",
        "RGB*, GRAY*",
        GIMP_PLUGIN,
        G_N_ELEMENTS(save_args), 0,
        save_args, NULL);

    gimp_register_file_handler_mime(SAVE_PROC, "image/x-ast26");
    gimp_register_save_handler(SAVE_PROC, "ast", "");
}

static void run(const gchar*     name,
                gint             nparams,
                const GimpParam* param,
                gint*            nreturn_vals,
                GimpParam**      return_vals) {
    static GimpParam values[2];
    *nreturn_vals = 1;
    *return_vals  = values;
    values[0].type          = GIMP_PDB_STATUS;
    values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

    gegl_init(NULL, NULL);

    if (std::strcmp(name, LOAD_PROC) == 0) {
        if (nparams < 3) return;
        const gchar* filename = param[1].data.d_string;

        gint32 image_id = load_ast(filename);
        if (image_id < 0) return;

        *nreturn_vals = 2;
        values[0].data.d_status = GIMP_PDB_SUCCESS;
        values[1].type          = GIMP_PDB_IMAGE;
        values[1].data.d_image  = image_id;

    } else if (std::strcmp(name, SAVE_PROC) == 0) {
        if (nparams < 5) return;

        GimpRunMode run_mode =
            static_cast<GimpRunMode>(param[0].data.d_int32);
        gint32 image_id   = param[1].data.d_image;
        gint32 drawable_id = param[2].data.d_drawable;
        const gchar* filename = param[3].data.d_string;

        // Try to read block size from the image parasite (original AST metadata)
        GimpParasite* p = gimp_image_get_parasite(image_id, "ast26-block-size");
        if (p) {
            int bx = 0, by = 0;
            if (sscanf(static_cast<const char*>(gimp_parasite_data(p)),
                       "%dx%d", &bx, &by) == 2 && bx > 0 && by > 0) {
                save_cfg.block_x = bx;
                save_cfg.block_y = by;
            }
            gimp_parasite_free(p);
        }

        switch (run_mode) {
            case GIMP_RUN_INTERACTIVE:
                gimp_get_data(SAVE_PROC, &save_cfg);
                if (!save_dialog()) {
                    values[0].data.d_status = GIMP_PDB_CANCEL;
                    return;
                }
                break;
            case GIMP_RUN_WITH_LAST_VALS:
                gimp_get_data(SAVE_PROC, &save_cfg);
                break;
            default:
                break;
        }

        // Convert to RGB if needed
        if (gimp_image_get_color_profile(image_id))
            gimp_image_convert_color_profile(
                image_id,
                gimp_color_profile_new_rgb_srgb(),
                GIMP_COLOR_RENDERING_INTENT_RELATIVE_COLORIMETRIC,
                TRUE);

        GimpExportReturn export_ret = gimp_export_image(
            &image_id, &drawable_id, "AST",
            static_cast<GimpExportCapabilities>(
                GIMP_EXPORT_CAN_HANDLE_RGB |
                GIMP_EXPORT_CAN_HANDLE_ALPHA));

        if (export_ret == GIMP_EXPORT_CANCEL) {
            values[0].data.d_status = GIMP_PDB_CANCEL;
            return;
        }

        GimpPDBStatusType status = save_ast(image_id, drawable_id, filename);
        values[0].data.d_status = status;

        if (status == GIMP_PDB_SUCCESS)
            gimp_set_data(SAVE_PROC, &save_cfg, sizeof(save_cfg));

        if (export_ret == GIMP_EXPORT_EXPORT)
            gimp_image_delete(image_id);
    }
}

GimpPlugInInfo PLUG_IN_INFO = {
    NULL,   // init
    NULL,   // quit
    query,  // query
    run     // run
};

MAIN()
