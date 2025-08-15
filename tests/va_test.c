// va_test.c
#include <stdio.h>
#include <fcntl.h>
#include <va/va.h>
#include <va/va_drm.h>
#include <unistd.h>

int main() {
    int fd = open("/dev/dri/renderD128", O_RDWR);
    if (fd < 0) { perror("open renderD128"); return 1; }

    VADisplay dpy = vaGetDisplayDRM(fd);
    int major=0, minor=0;
    VAStatus st = vaInitialize(dpy, &major, &minor);
    if (st == VA_STATUS_SUCCESS) {
        printf("VAAPI OK. Version %d.%d, vendor: %s\n",
               major, minor, vaQueryVendorString(dpy));
        vaTerminate(dpy);
    } else {
        printf("VAAPI init failed: %d\n", st);
    }
    close(fd);
    return 0;
}

