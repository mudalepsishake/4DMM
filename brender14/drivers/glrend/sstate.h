/* BRenderModern:
 * Stored renderer state
 */
#ifndef _SSTATE_H_
#define _SSTATE_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef BR_RENDERER_STATE_STORED_PRIVATE

/* BRenderModern:
 * Private state of geometry format
 */
typedef struct br_renderer_state_stored {
    /* BRenderModern:
     * Dispatch table
     */
    const struct br_renderer_state_stored_dispatch *dispatch;

    /* BRenderModern:
     * Standard object identifier
     */
    const char *identifier;

    /* BRenderModern:
     * Pointer to owning device
     */
    struct br_device *device;

    /* BRenderModern:
     * Saved state
     */

    /* BRenderModern:
     * Pointer to renderer that this state is asociated with
     */
    struct br_renderer *renderer;

    state_stack state;
} br_renderer_state_stored;

#endif

#ifdef __cplusplus
};
#endif
#endif
