#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <isa-l.h> // ISA-L başlık dosyasını ekle

#include "gpu_detect_factory.h"

// --- FEC Parametreleri ---
#define K_DATA_SHARDS 10
#define R_PARITY_SHARDS 4
#define TOTAL_SHARDS (K_DATA_SHARDS + R_PARITY_SHARDS)

// AppSink'ten yeni bir örnek (H.264 buffer) geldiğinde çağrılacak fonksiyon
static GstFlowReturn on_new_sample_from_sink(GstElement *sink, gpointer data) {
    GstSample *sample;
    GstBuffer *buffer;
    GstMapInfo map;

    // AppSink'ten örneği çek
    g_signal_emit_by_name(sink, "pull-sample", &sample);
    if (!sample) return GST_FLOW_OK;

    buffer = gst_sample_get_buffer(sample);
    if (!buffer) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        g_print("H.264 NAL unit received, size: %ld bytes. Applying FEC...\n", map.size);

        // --- ISA-L Reed-Solomon Kodlama Başlangıcı ---
        
        // 1. Gelen veriyi shard'lara bölmek için boyutu ve hizalamayı hesapla
        int shard_size = (map.size + K_DATA_SHARDS - 1) / K_DATA_SHARDS;
        // ISA-L'in verimli çalışması için shard boyutunu 16 byte'ın katı yapalım
        shard_size = (shard_size + 15) & ~15;

        // 2. Bellek tamponlarını (buffer) ayarla
        unsigned char *data_shards[TOTAL_SHARDS];
        for (int i = 0; i < TOTAL_SHARDS; i++) {
            if (posix_memalign((void **)&data_shards[i], 16, shard_size)) {
                g_printerr("Failed to allocate aligned memory for shards\n");
                gst_buffer_unmap(buffer, &map);
                gst_sample_unref(sample);
                return GST_FLOW_ERROR;
            }
        }
        
        // Gelen veriyi ilk K adet shard'a kopyala, kalanı sıfırla (padding)
        // memset(data_shards[0], 0, K_DATA_SHARDS * shard_size);
        // memcpy(data_shards[0], map.data, map.size);

        // Daha güvenli bellek yönetimi: Veriyi shard'lara manuel olarak dağıt
        size_t bytes_copied = 0;
        for (int i = 0; i < K_DATA_SHARDS; i++) {
            size_t to_copy = map.size - bytes_copied;
            if (to_copy > shard_size) {
                to_copy = shard_size;
            }
            if (to_copy > 0) {
                memcpy(data_shards[i], (unsigned char*)map.data + bytes_copied, to_copy);
                bytes_copied += to_copy;
            }
            // Shard'ın geri kalanını sıfırla (padding)
            if (to_copy < shard_size) {
                memset(data_shards[i] + to_copy, 0, shard_size - to_copy);
            }
        }
        // Eğer veri tüm shard'ları doldurmadıysa, kalan shard'ları tamamen sıfırla
        for (int i = bytes_copied / shard_size + 1; i < K_DATA_SHARDS; i++) {
             memset(data_shards[i], 0, shard_size);
        }

        // 3. Kodlama matrisini ve tablolarını oluştur
        unsigned char gftbls[K_DATA_SHARDS * R_PARITY_SHARDS * 32];
        unsigned char encode_matrix[TOTAL_SHARDS * K_DATA_SHARDS];
        ec_init_tables(K_DATA_SHARDS, R_PARITY_SHARDS, &encode_matrix[K_DATA_SHARDS * K_DATA_SHARDS], gftbls);
        gf_gen_cauchy1_matrix(encode_matrix, TOTAL_SHARDS, K_DATA_SHARDS);

        // 4. Parity dilimlerini hesapla (asıl kodlama işlemi)
        ec_encode_data(shard_size, K_DATA_SHARDS, R_PARITY_SHARDS, gftbls, data_shards, &data_shards[K_DATA_SHARDS]);

        g_print(" > FEC applied: %d data + %d parity shards of size %d created.\n", K_DATA_SHARDS, R_PARITY_SHARDS, shard_size);
        
        // TODO: Burada k+r adet shard, RTP paketlerine dönüştürülüp gönderilecek
        
        // Belleği temizle
        for (int i = 0; i < TOTAL_SHARDS; i++) {
            free(data_shards[i]);
        }
        // --- ISA-L Reed-Solomon Kodlama Sonu ---

        gst_buffer_unmap(buffer, &map);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

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

    const char *post_name = gpu_detect_and_find_factory("postproc");
    const char *enc_name  = gpu_detect_and_find_factory("encoder");
    const char *sink_name = gpu_detect_and_find_factory("sink");

    // Pipeline elemanları
    GstElement *pipeline = gst_pipeline_new("SenderEngine");
    GstElement *src      = gst_element_factory_make("v4l2src", "src");
    GstElement *mjpg_cf  = gst_element_factory_make("capsfilter", "mjpg_caps");
    GstElement *dec      = NULL;
    GstElement *post     = NULL;
    GstElement *tee      = gst_element_factory_make("tee", "tee");

    // Önizleme Dalı Elemanları
    GstElement *q_disp   = gst_element_factory_make("queue", "q_disp");
    GstElement *sink     = NULL;

    // Yayın Dalı Elemanları
    GstElement *q_enc    = gst_element_factory_make("queue", "q_enc");
    GstElement *enc      = NULL;
    GstElement *parse    = gst_element_factory_make("h264parse", "parse");
    GstElement *appsink  = gst_element_factory_make("appsink", "encoder_output");

    if (!pipeline || !src || !mjpg_cf || !tee || !q_disp || !q_enc || !parse || !appsink) {
        g_printerr("Failed to create base elements\n");
        return 1;
    }
    
    // Cihaz ayarları
    g_object_set(src, "device", "/dev/video0", NULL);

    // Kamera 720p@30 MJPG caps
    GstCaps *mjpg_caps = gst_caps_new_simple("image/jpeg",
        "width",  G_TYPE_INT, 1280,
        "height", G_TYPE_INT, 720,
        "framerate", GST_TYPE_FRACTION, 30, 1,
        NULL);
    g_object_set(mjpg_cf, "caps", mjpg_caps, NULL);
    gst_caps_unref(mjpg_caps);

    // GPU'ya göre JPEG decoder seçimi
    // Not: Bu mantık, sink yerine encoder'a göre de genişletilebilir.
    if ((post_name && str_contains(post_name, "vaapi")) ||
        (enc_name && str_contains(enc_name, "vaapi")) ||
        gst_element_factory_find("vaapijpegdec")) {
        dec = gst_element_factory_make("vaapijpegdec", "dec");
    } else if ((post_name && (str_contains(post_name, "nv") || str_contains(post_name, "cuda"))) ||
               (enc_name && (str_contains(enc_name, "nv") || str_contains(enc_name, "cuda"))) ||
               gst_element_factory_find("nvjpegdec")) {
        dec = gst_element_factory_make("nvjpegdec", "dec"); // NV codec kuruluysa
    } else {
        dec = gst_element_factory_make("jpegdec", "dec");   // CPU fallback
    }
    if (!dec) { g_printerr("Failed to create JPEG decoder\n"); return 1; }

    // Post-proc (opsiyonel)
    if (post_name) post = gst_element_factory_make(post_name, "gpu_post");

    // Sink (Önizleme) seçimi
    if (sink_name) sink = gst_element_factory_make(sink_name, "gpu_sink");
    if (!sink)     sink = gst_element_factory_make("autovideosink", "cpu_sink");
    if (!sink)   { g_printerr("Failed to create sink\n"); return 1; }

    // Encoder (Yayın) seçimi
    if (enc_name) {
        enc = gst_element_factory_make(enc_name, "gpu_enc");
        if (g_str_has_prefix(enc_name, "vaapi")) {
            // "film" tune ayarı genellikle daha iyi kalite verir
            g_object_set(enc, "tune", 1, NULL); 
        } else if (g_str_has_prefix(enc_name, "nv")) {
             // p7 en yüksek kalite, hq yüksek kalite tune
            g_object_set(enc, "preset", 7, "tune", 4, NULL);
        }
    } else {
        enc = gst_element_factory_make("x264enc", "cpu_enc");
        // CPU için "film" tune ayarı
        g_object_set(enc, "tune", 4, NULL);
    }
    if (!enc) { g_printerr("Failed to create encoder\n"); return 1; }

    // AppSink ayarları
    g_object_set(appsink, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(appsink, "new-sample", G_CALLBACK(on_new_sample_from_sink), NULL);

    // Pipeline’a elemanları ekle
    if (post)
        gst_bin_add_many(GST_BIN(pipeline), src, mjpg_cf, dec, post, tee, 
                         q_disp, sink, q_enc, enc, parse, appsink, NULL);
    else
        gst_bin_add_many(GST_BIN(pipeline), src, mjpg_cf, dec, tee, 
                         q_disp, sink, q_enc, enc, parse, appsink, NULL);

    // Elemanları bağla
    // 1. Ana hattı tee'ye kadar bağla
    gboolean linked = FALSE;
    if (post)
        linked = gst_element_link_many(src, mjpg_cf, dec, post, tee, NULL);
    else
        linked = gst_element_link_many(src, mjpg_cf, dec, tee, NULL);
    if (!linked) { g_printerr("Failed to link main path to tee\n"); return 1; }

    // 2. Tee'den dalları bağla
    // Önizleme Dalı
    if (!gst_element_link_many(q_disp, sink, NULL)) {
         g_printerr("Failed to link display branch\n"); return 1;
    }
    GstPad *tee_disp_pad = gst_element_get_request_pad(tee, "src_%u");
    GstPad *q_disp_pad = gst_element_get_static_pad(q_disp, "sink");
    if (gst_pad_link(tee_disp_pad, q_disp_pad) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link tee to display queue\n"); return 1;
    }
    gst_object_unref(tee_disp_pad);
    gst_object_unref(q_disp_pad);

    // Yayın Dalı
    if (!gst_element_link_many(q_enc, enc, parse, appsink, NULL)) {
        g_printerr("Failed to link encode branch\n"); return 1;
    }
    GstPad *tee_enc_pad = gst_element_get_request_pad(tee, "src_%u");
    GstPad *q_enc_pad = gst_element_get_static_pad(q_enc, "sink");
    if (gst_pad_link(tee_enc_pad, q_enc_pad) != GST_PAD_LINK_OK) {
        g_printerr("Failed to link tee to encode queue\n"); return 1;
    }
    gst_object_unref(tee_enc_pad);
    gst_object_unref(q_enc_pad);

    // Bus & run
    GstBus *bus = gst_element_get_bus(pipeline);
    gst_bus_add_watch(bus, bus_call, loop);
    gst_object_unref(bus);

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    g_print("Starting sender pipeline with local preview...\n");
    const char *dec_name_str = gst_element_factory_find("vaapijpegdec") ? "vaapijpegdec" :
                               (gst_element_factory_find("nvjpegdec") ? "nvjpegdec" : "jpegdec (CPU)");
    g_print(" > Decode on: %s\n", dec_name_str);
    g_print(" > Encode on: %s\n", enc_name ? enc_name : "x264enc (CPU fallback)");
    g_main_loop_run(loop);

    g_print("Stopping sender pipeline.\n");
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    g_main_loop_unref(loop);
    return 0;
}
