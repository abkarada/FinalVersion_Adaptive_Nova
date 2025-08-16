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
   
    const char *dec_name = NULL;
    if (strstr(sink_name, "vaapi") || strstr(post_name, "vaapi")) {
        dec_name = "vaapijpegdec";
    } else if (strstr(sink_name, "nv") || strstr(post_name, "nv")) {
        dec_name = "nvjpegdec";
    } else {
        dec_name = "jpegdec"; // Fallback
    }

    GstElement *dec      = gst_element_factory_make(dec_name, "dec");
    GstElement *post     = NULL;
    GstElement *sink     = NULL;
    
    if (!pipeline || !src || !mjpg_cf || !dec) {
        g_printerr("Failed to create base or decoder elements\n");
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

    if (post_name) post = gst_element_factory_make(post_name, "gpu_post");

    if (sink_name) sink = gst_element_factory_make(sink_name, "gpu_sink");
    if (!sink)     sink = gst_element_factory_make("autovideosink", "sink");
    if (!sink)   { g_printerr("Failed to create sink\n"); return 1; }

    g_print(" > Selected postproc: %s\n", post_name ? post_name : "none");
    g_print(" > Selected sink: %s\n", sink_name ? sink_name : "autovideosink (fallback)");
    g_print(" > Selected decoder: %s\n", dec_name);

    g_object_set(sink, "sync", FALSE, NULL);

    if (post) {
        GstElement *gpu_cf = gst_element_factory_make("capsfilter", "gpu_caps");
        if (!gpu_cf) { g_printerr("Failed to create gpu capsfilter\n"); return 1; }

        GstCaps *gpu_caps = gst_caps_from_string("video/x-raw(memory:DMABuf)");
        if (post_name && strstr(post_name, "vaapi")) {
            gst_caps_unref(gpu_caps);
            gpu_caps = gst_caps_from_string("video/x-raw(memory:VASurface)");
        }
        g_object_set(gpu_cf, "caps", gpu_caps, NULL);
        gst_caps_unref(gpu_caps);

        gst_bin_add_many(GST_BIN(pipeline), src, mjpg_cf, dec, post, gpu_cf, sink, NULL);
        gboolean linked = gst_element_link_many(src, mjpg_cf, dec, post, gpu_cf, sink, NULL);
        if (!linked) {
            g_printerr("Failed to link elements with GPU memory caps\n");
            return 1;
        }
    } else {
        gst_bin_add_many(GST_BIN(pipeline), src, mjpg_cf, dec, sink, NULL);
        gboolean linked = gst_element_link_many(src, mjpg_cf, dec, sink, NULL);
        if (!linked) {
            g_printerr("Failed to link elements (simple)\n");
            return 1;
        }
    }


    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("Running camera preview...\n");
    g_main_loop_run(loop);

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    return 0;
}
