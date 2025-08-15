#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu_detect_factory.h"

static GpuVendor detect_gpu_vendor_lspci(void) {
    FILE *fp = popen("lspci -nn | grep -i 'vga\\|3d\\|display'", "r");
    if (!fp) return GPU_UNKNOWN;
    char line[512]; GpuVendor v = GPU_UNKNOWN;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "8086")) v = GPU_INTEL; // Intel
        else if (strstr(line, "10de")) v = GPU_NVIDIA; // NVIDIA
        else if (strstr(line, "1002") || strstr(line, "1022")) v = GPU_AMD; // AMD
    }
    pclose(fp);
    return v;
}

static int has_elem(const char *name) {
    GstElementFactory *f = gst_element_factory_find(name);
    if (f) { gst_object_unref(f); return 1; }
    return 0;
}

/* 
   role:
     "decoder"  -> GPU decode elementi
     "encoder"  -> GPU encode elementi
     "sink"     -> GPU sink elementi
     "postproc" -> GPU postprocess elementi
*/
const char* gpu_detect_and_find_factory(const char *role) {
    GpuVendor v = detect_gpu_vendor_lspci();

    // VAAPI (Intel/AMD)
    if ((v == GPU_INTEL || v == GPU_AMD) && has_elem("vaapidecodebin")) {
        if (!strcmp(role,"decoder"))  return "vaapidecodebin";
        if (!strcmp(role,"encoder"))  return "vaapih264enc";
        if (!strcmp(role,"sink"))     return "vaapisink";
        if (!strcmp(role,"postproc")) return "vaapipostproc";
    }

    // NVIDIA
    if (v == GPU_NVIDIA && has_elem("nvv4l2decoder")) {
        if (!strcmp(role,"decoder"))  return "nvv4l2decoder";
        if (!strcmp(role,"encoder"))  return "nvh264enc";
        if (!strcmp(role,"sink")) {
            if (has_elem("nveglglessink")) return "nveglglessink";
            else if (has_elem("nv3dsink")) return "nv3dsink";
        }
        if (!strcmp(role,"postproc")) return "nvvideoconvert";
    }

    return NULL; // hızlandırma yok
}

