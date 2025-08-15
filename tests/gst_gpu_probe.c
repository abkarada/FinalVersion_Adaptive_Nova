// gst_gpu_probe.c
#include <gst/gst.h>
#include <stdio.h>

static gboolean has_elem(const char *name) {
    GstElementFactory *f = gst_element_factory_find(name);
    if (f) { gst_object_unref(f); return TRUE; }
    return FALSE;
}

typedef enum { PIPE_CPU=0, PIPE_INTEL_VAAPI, PIPE_NVIDIA, PIPE_AMD_VAAPI } PipelineHint;

PipelineHint choose_pipeline_by_gst(void) {
    // NVIDIA tarafı (dağıtımına göre isimler değişebilir)
    if (has_elem("nvh264enc") || has_elem("nvv4l2decoder") || has_elem("nvh265enc"))
        return PIPE_NVIDIA;

    // Intel/AMD VAAPI (aynı VAAPI elemanları iki tarafta da olabilir)
    if (has_elem("vaapih264enc") || has_elem("vaapipostproc") || has_elem("vaapih265enc") || has_elem("vaapidecodebin")) {
        // Markayı ayrıca DRM katmanından öğrenebilirsin (opsiyonel)
        return PIPE_INTEL_VAAPI; // veya PIPE_AMD_VAAPI; (A katmanından gelen bilgiyle ayrıştır)
    }

    // Hiçbiri yoksa CPU yolu
    return PIPE_CPU;
}

#ifdef TEST_GST_PROBE
int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    PipelineHint h = choose_pipeline_by_gst();
    printf("GST pipeline hint: %d\n", (int)h);
    return 0;
}
#endif
