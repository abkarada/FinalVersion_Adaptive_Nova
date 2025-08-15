#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    GPU_UNKNOWN = 0,
    GPU_INTEL,
    GPU_NVIDIA,
    GPU_AMD
} GpuVendor;

const char* gpu_vendor_name(GpuVendor v) {
    switch (v) {
        case GPU_INTEL:  return "Intel";
        case GPU_NVIDIA: return "NVIDIA";
        case GPU_AMD:    return "AMD";
        default:         return "Unknown";
    }
}

GpuVendor detect_gpu_vendor_lspci(void) {
    FILE *fp = popen("lspci -nn | grep -i 'vga\\|3d\\|display'", "r");
    if (!fp) return GPU_UNKNOWN;

    char line[512];
    GpuVendor vendor = GPU_UNKNOWN;

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "8086")) { // Intel PCI ID
            vendor = GPU_INTEL;
        } else if (strstr(line, "10de")) { // NVIDIA PCI ID
            vendor = GPU_NVIDIA;
        } else if (strstr(line, "1002") || strstr(line, "1022")) { // AMD/ATI PCI ID
            vendor = GPU_AMD;
        }
    }

    pclose(fp);
    return vendor;
}

#ifdef TEST_LSPCI
int main() {
    GpuVendor v = detect_gpu_vendor_lspci();
    printf("Detected GPU: %s\n", gpu_vendor_name(v));
    return 0;
}
#endif

