/* SPDX-License-Identifier: LGPL-2.1+ */
/* Copyright (c) 2019 Lucas De Marchi <lucas.de.marchi@gmail.com> */

#include "controller.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "util.h"

enum InfoAbs {
    INFO_ABS_MIN,
    INFO_ABS_MAX,
    _INFO_ABS_COUNT,
};

enum Axis {
    AXIS_ROLL,
    AXIS_PITCH,
    AXIS_THROTTLE,
    AXIS_YAW,
    AXIS_AUX_LEFT,
    _AXIS_COUNT,
};

struct evdev_abs_axis {
    int value;
    int min;
    int max;
    int fuzz;
    int flat;
    int resolution;
};

struct Controller {
    int fd;

    int val[_AXIS_COUNT];
    struct {
        int range[_AXIS_COUNT][_INFO_ABS_COUNT];
    } info;
};

static struct Controller controller;

/* Currently we have a static map for SkyController 2 */
static const int evdev_mapping[] = {
    [AXIS_ROLL] = ABS_Z, [AXIS_PITCH] = ABS_RX,    [AXIS_THROTTLE] = ABS_Y,
    [AXIS_YAW] = ABS_X,  [AXIS_AUX_LEFT] = ABS_RY,
};

static int get_axis_from_evdev(unsigned long code)
{
    int e;

    for (e = 0; e < _AXIS_COUNT; e++)
        if (evdev_mapping[e] == (int)code)
            return e;

    return -1;
}

static int evdev_fill_info(int fd, struct Controller *c)
{
    /* query events and codes supported */
    unsigned long mask[BITMASK_NLONGS(KEY_MAX)];
    unsigned long code;
    unsigned int naxis = 0;

    memset(mask, 0, sizeof(mask));
    ioctl(fd, EVIOCGBIT(0, EV_MAX), mask);
    if (!test_bit(EV_ABS, mask)) {
        fprintf(stderr, "EV_ABS event is not supported\n");
        return -EINVAL;
    }

    memset(mask, 0, sizeof(mask));
    ioctl(fd, EVIOCGBIT(EV_ABS, KEY_MAX), mask);
    for (code = 0; code < EV_MAX && naxis < _AXIS_COUNT; code++) {
        struct evdev_abs_axis abs;
        int axis;

        if (!test_bit(code, mask))
            continue;

        axis = get_axis_from_evdev(code);
        if (axis < 0)
            continue;

        memset(&abs, 0, sizeof(abs));
        ioctl(fd, EVIOCGABS(code), &abs);

        /* fill struct */
        c->info.range[axis][INFO_ABS_MIN] = abs.min;
        c->info.range[axis][INFO_ABS_MAX] = abs.max;

        printf("axis: %d min: %d max: %d\n", axis, abs.min, abs.max);

        naxis++;
    }

    if (naxis < _AXIS_COUNT) {
        fprintf(stderr, "Not all required axis supported by this input\n");
        return -EINVAL;
    }

    printf("controller ok\n");
    return 0;
}

int controller_init(const char *device)
{
    int fd, r;

    controller.fd = -1;

    fd = open(device, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "error: can't open %s: %m", device);
        return -errno;
    }

    /* TODO: use EVIOCGID and check device id to support other controllers */
    r = evdev_fill_info(fd, &controller);
    if (r < 0)
        goto fail_fill;

    controller.fd = fd;

    return 0;

fail_fill:
    close(fd);
    return r;
}

void controller_shutdown(void)
{
    if (controller.fd < 0)
        return;

    close(controller.fd);
}