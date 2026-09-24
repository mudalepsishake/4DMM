/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: fw.h 1.1 1997/12/10 16:41:07 jon Exp $
 * $Locker: $
 *
 * Internal types and structures for framework
 */
#ifndef _FW_H_
#define _FW_H_

/* BRender:
 * Pull in all the public definitions/declarations
 */
#ifndef _BRDDI_H_
#include "brddi.h"
#endif

/* BRender:
 * Private state of framework
 */
typedef struct br_framework_state {
    /* BRender:
     * Flag to indicate that framework is set up
     */
    br_boolean active;

    /* BRender:
     * Various lists of registered items
     */
    br_registry reg_resource_classes;

    /* BRender:
     * An index of registered resources by class
     */
    br_resource_class *resource_class_index[BR_MAX_RESOURCE_CLASSES];

    /* BRender:
     * Collection of tokens
     */
    br_list  tokens;
    br_token next_free_token;

    /* BRender:
     * Current filesystem, memory, and error handlers
     */
    br_filesystem  *fsys;
    br_allocator   *mem;
    br_diaghandler *diag;
    br_loghandler  *log;

    /* BRenderModern:
     * Logging level
     */
    int log_level;

    /* BRender:
     * File write mode
     */
    int open_mode;

    /* BRender:
     * Base resource of which everything else is a descendant
     */
    void *res;

    /* BRender:
     * Global scratch space
     */
    void     *scratch_ptr;
    br_size_t scratch_size;
    br_size_t scratch_last;
    int       scratch_inuse;

    /* BRender:
     * error value
     */
    br_error last_error_type;
    void   **last_error_value;
    char     last_error_string[128];

    /* BRender:
     * List of loaded images
     */
    br_list images;

    /* BRender:
     * Pointers to loaded devices and the images whence they came
     */
    struct br_open_device *dev_slots;
    br_int_32              ndev_slots;

    br_associative_array *sys_config;
    br_boolean            bAlreadyLoadedDrivers;

} br_framework_state;

/* BRender:
 * Device pointer and image from where it came (or NULL if device is static).
 */
typedef struct br_open_device {
    struct br_device *dev;
    struct br_image  *image;
} br_open_device;

/* BRender:
 * Global renderer state
 */
#ifdef __cplusplus
extern "C" {
#endif
extern br_framework_state BR_ASM_DATA fw;
#ifdef __cplusplus
};
#endif

/* BRender:
 * Minimum scratch space to allocate for render temps.
 */
#define MIN_WORKSPACE 8192

#if DEBUG
/* BRender:
 * Controls whether the source file/line of each
 * resource allocation is tracked
 *
 * XXX Unimplemented
 */
#define BR_RES_TRACKING 1

/* BRender:
 * True if resources are tagged with a magic number and pointer - allows validation
 * and debug dumping
 */
#define BR_RES_TAGGING 1
#else
#define BR_RES_TRACKING 1
#define BR_RES_TAGGING  1
#endif

/* BRender:
 * Initial number of device slots
 */
#define NDEV_SLOTS 16

/* BRender:
 * Pull in private prototypes
 */
#ifndef _NO_PROTOTYPES

#ifndef _FW_IP_H_
#include "fw_ip.h"
#endif

#endif
#endif
