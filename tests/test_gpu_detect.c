// test_gpu_detect.c (sadece denemek için)
#include <stdio.h>
#include "gpu_detect.h"

int main() {
    GpuVendor v = detect_gpu_vendor_drm();
    printf("Detected GPU: %s\n", gpu_vendor_name(v));
    return 0;
}

