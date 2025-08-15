#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu_detect_factory.h"

static gboolean bus_call(GstBus *bus, GstMessage *msg, gpointer data) {
    GMainLoop *loop = (GMainLoop *)data;
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError *err = NULL; gchar *dbg = NULL;
            gst_message_parse_error(msg, &err, &dbg);
            g_printerr("ERROR: %s\n", err->message);
            if (dbg) g_printerr("Debug: %s\n", dbg);
            g_clear_error(&err); g_free(dbg);
            g_main_loop_quit(loop);
            break;
        }
        case GST_MESSAGE_EOS:
            g_print("EOS\n");
            g_main_loop_quit(loop);
            break;
        default: break;
    }
    return TRUE;
}

static gboolean str_contains(const char *s, const char *needle) {
    return s && needle && strstr(s, needle) != NULL;
}

int main(int argc, char **argv)
{
    gst_init(&argc, &argv);
    GMainLoop *loop = g_main_loop_new(NULL, FALSE);

    const char *post_name = gpu_detect_and_find_factory("postproc"); //  "vaapipostproc", "nvvideoconvert"
    const char *sink_name = gpu_detect_and_find_factory("sink");     //  "vaapisink", "nveglglessink", "glimagesink"

    GstElement *pipeline = gst_pipeline_new("Cam720p_FPS_Test");
    GstElement *src      = gst_element_factory_make("v4l2src", "src");
    GstElement *mjpg_cf  = gst_element_factory_make("capsfilter", "mjpg_caps");
   
    GstElement *dec      = NULL;
    GstElement *post     = NULL;
    GstElement *sink     = NULL;
    
    GstElement *fps_sink = gst_element_factory_make("fpsdisplaysink", "fps");

    if (!pipeline || !src || !mjpg_cf || !fps_sink) {
        g_printerr("Failed to create base or fps elements\n");
        return 1;
    }

    g_object_set(src, "device", "/dev/video0", NULL);
    // g_object_set(src, "extra-controls", "c,compression_quality=95", NULL);

    //  MJPG veriyor → src den JPEG caps iste
    GstCaps *mjpg_caps = gst_caps_new_simple("image/jpeg",
        "width",  G_TYPE_INT, 1280,
        "height", G_TYPE_INT, 720,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        NULL);
    g_object_set(mjpg_cf, "caps", mjpg_caps, NULL);
    gst_caps_unref(mjpg_caps);

    if ((post_name && str_contains(post_name, "vaapi")) ||
        (sink_name && str_contains(sink_name, "vaapi")) ||
        gst_element_factory_find("vaapijpegdec")) {
        dec = gst_element_factory_make("vaapijpegdec", "dec");
    } else if ((post_name && (str_contains(post_name, "nv") || str_contains(post_name, "cuda"))) ||
               (sink_name && (str_contains(sink_name, "nv") || str_contains(sink_name, "cuda"))) ||
               gst_element_factory_find("nvjpegdec")) {
        dec = gst_element_factory_make("nvjpegdec", "dec"); // NV codec kuruluysa
    } else {
        dec = gst_element_factory_make("jpegdec", "dec");   // CPU fallback
    }
    if (!dec) { g_printerr("Failed to create JPEG decoder\n"); return 1; }

    // Post-proc (opsiyonel): varsa ekle; yoksa atla
    if (post_name) post = gst_element_factory_make(post_name, "gpu_post");

    // Sink seçimi: önce tespit edilen, yoksa autovideosink
    if (sink_name) sink = gst_element_factory_make(sink_name, "gpu_sink");
    if (!sink)     sink = gst_element_factory_make("autovideosink", "sink");
    if (!sink)   { g_printerr("Failed to create sink\n"); return 1; }

    // fpsdisplaysink'in video-sink özelliğini bizim GPU sink'imizle ayarla
    g_object_set(fps_sink, "video-sink", sink, "sync", FALSE, NULL);

    // Pipeline’a elemanları ekle
    if (post)
        gst_bin_add_many(GST_BIN(pipeline), src, mjpg_cf, dec, post, fps_sink, NULL);
    else
        gst_bin_add_many(GST_BIN(pipeline), src, mjpg_cf, dec, fps_sink, NULL);


    // Bağlantılar
    gboolean linked = FALSE;
    if (post)
        linked = gst_element_link_many(src, mjpg_cf, dec, post, fps_sink, NULL);
    else
        linked = gst_element_link_many(src, mjpg_cf, dec, fps_sink, NULL);

    if (!linked) {
        g_printerr("Failed to link elements (likely caps mismatch)\n");
        return 1;
    }

    // Bus & run
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("Running camera preview with FPS counter...\n");
    g_print(" > Decode on: %s\n",
            (gst_element_factory_find("vaapijpegdec") ? "VAAPI" :
             gst_element_factory_find("nvjpegdec") ? "NVDEC" : "CPU"));
    g_main_loop_run(loop);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    return 0;
}
