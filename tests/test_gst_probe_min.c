// test_gst_probe_min.c
#include <stdio.h>
#include <gst/gst.h>
#include "gst_probe_min.c"  // pratik olsun diye doğrudan dahil ediyoruz

int main(int argc, char **argv) {
    gst_init(&argc, &argv);
    printf("VAAPI? %s\n", gst_has_vaapi() ? "YES" : "NO");
    printf("NVCodec? %s\n", gst_has_nvcodec() ? "YES" : "NO");
    return 0;
}

