#ifndef GPU_DETECT_FACTORY_H
#define GPU_DETECT_FACTORY_H

#include <gst/gst.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GPU üretici tipi */
typedef enum { 
    GPU_UNKNOWN = 0, 
    GPU_INTEL, 
    GPU_NVIDIA, 
    GPU_AMD 
} GpuVendor;

/**
 * GPU'yu tespit edip GStreamer element factory adını döndürür.
 * 
 * @param role:
 *   "decoder"  -> GPU decode elementi
 *   "encoder"  -> GPU encode elementi
 *   "sink"     -> GPU sink elementi
 *   "postproc" -> GPU postprocess elementi
 * @return const char*  -> GStreamer element factory adı (örn. "vaapipostproc")
 *                         NULL -> GPU hızlandırma yok
 */
const char* gpu_detect_and_find_factory(const char *role);

#endif // GPU_DETECT_FACTORY_H

