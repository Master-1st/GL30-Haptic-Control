#ifndef GL30_UI_ANIM_VIEW_H_
#define GL30_UI_ANIM_VIEW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "gl30_ui_anim_model.h"
#include "gl30_ui_app.h"

typedef struct gl30_ui_anim_view_t gl30_ui_anim_view_t;
typedef void (*gl30_ui_action_handler_t)(void* context, gl30_ui_action_t action);

/* Called on the owning LVGL task. Handler updates app state, never GPIO/motor. */
void gl30_ui_view_set_action_handler(gl30_ui_anim_view_t* view,
                                    gl30_ui_action_handler_t handler, void* context);

gl30_ui_anim_view_t* gl30_ui_view_create(void* parent_obj, uint16_t canvas_size);

void gl30_ui_view_destroy(gl30_ui_anim_view_t* view);

void gl30_ui_view_render(gl30_ui_anim_view_t* view, const gl30_ui_anim_output_t* output);

#ifdef __cplusplus
}
#endif

#endif
