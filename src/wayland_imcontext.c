/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <Ecore.h>
#include <Ecore_Evas.h>
#include <Ecore_Wayland.h>

#include "wayland_imcontext.h"

struct _WaylandIMContext
{
   Ecore_IMF_Context *ctx;

   struct text_model_factory *text_model_factory;
   struct text_model *text_model;

   Ecore_Wl_Window *window;

   char *preedit_text;
   char *preedit_commit;
   Eina_List *preedit_attrs;
   int32_t preedit_cursor;

   struct
     {
        Eina_List *attrs;
        int32_t cursor;
     } preedit_info;

   xkb_mod_mask_t control_mask;
   xkb_mod_mask_t alt_mask;
   xkb_mod_mask_t shift_mask;

   uint32_t serial;
};

static void
text_model_commit_string(void              *data,
                         struct text_model *text_model,
			 uint32_t           serial,
                         const char        *text,
                         uint32_t           index)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)data;

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom,
                     "commit event (text: `%s', current pre-edit: `%s')",
                     text,
                     imcontext->preedit_text ? imcontext->preedit_text : "");

   if (imcontext->ctx)
     {
        ecore_imf_context_commit_event_add(imcontext->ctx, text);
        ecore_imf_context_event_callback_call(imcontext->ctx, ECORE_IMF_CALLBACK_COMMIT, (void *)text);
     }
}

static void
commit_preedit(WaylandIMContext *imcontext)
{
   if (!imcontext->preedit_commit)
     return;

   if (imcontext->ctx)
     {
        ecore_imf_context_commit_event_add(imcontext->ctx, imcontext->preedit_commit);
        ecore_imf_context_event_callback_call(imcontext->ctx, ECORE_IMF_CALLBACK_COMMIT, (void *)imcontext->preedit_commit);
     }
}

static void
clear_preedit(WaylandIMContext *imcontext)
{
   Ecore_IMF_Preedit_Attr *attr;

   imcontext->preedit_cursor = 0;

   free(imcontext->preedit_text);
   imcontext->preedit_text = NULL;

   free(imcontext->preedit_commit);
   imcontext->preedit_commit = NULL;

   EINA_LIST_FREE(imcontext->preedit_attrs, attr)
      free(attr);
   imcontext->preedit_attrs = NULL;
}

static void
text_model_preedit_string(void              *data,
                          struct text_model *text_model,
                          uint32_t           serial,
                          const char        *text,
                          const char        *commit)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)data;
   Eina_Bool old_preedit = EINA_FALSE;

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom,
                     "preedit event (text: `%s' index: %u, current pre-edit: `%s' index: %d)",
                     text, index,
                     imcontext->preedit_text ? imcontext->preedit_text : "",
                     imcontext->preedit_cursor);

   old_preedit = imcontext->preedit_text && strlen(imcontext->preedit_text) > 0;

   clear_preedit(imcontext);

   imcontext->preedit_text = strdup(text);
   imcontext->preedit_commit = strdup(commit);
   imcontext->preedit_cursor = imcontext->preedit_info.cursor;
   imcontext->preedit_attrs = imcontext->preedit_info.attrs;

   imcontext->preedit_info.attrs = NULL;

   if (!old_preedit)
     {
        ecore_imf_context_preedit_start_event_add(imcontext->ctx);
        ecore_imf_context_event_callback_call(imcontext->ctx, ECORE_IMF_CALLBACK_PREEDIT_START, NULL);
     }

   ecore_imf_context_preedit_changed_event_add(imcontext->ctx);
   ecore_imf_context_event_callback_call(imcontext->ctx, ECORE_IMF_CALLBACK_PREEDIT_CHANGED, NULL);

   if (strlen(imcontext->preedit_text) == 0)
     {
        ecore_imf_context_preedit_end_event_add(imcontext->ctx);
        ecore_imf_context_event_callback_call(imcontext->ctx, ECORE_IMF_CALLBACK_PREEDIT_END, NULL);
     }
}

static void
text_model_delete_surrounding_text(void              *data,
                                   struct text_model *text_model,
                                   uint32_t           serial,
                                   int32_t            index,
                                   uint32_t           length)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)data;
   Ecore_IMF_Event_Delete_Surrounding ev;

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom,
                     "delete surrounding text (index: %d, length: %u)",
                     index,
                     length);

   ev.ctx = imcontext->ctx;
   ev.offset = index;
   ev.n_chars = length;

   ecore_imf_context_delete_surrounding_event_add(imcontext->ctx, index, length);
   ecore_imf_context_event_callback_call(imcontext->ctx, ECORE_IMF_CALLBACK_DELETE_SURROUNDING, &ev);
}

static void
text_model_preedit_styling(void              *data,
			   struct text_model *text_model,
			   uint32_t           serial,
			   uint32_t           index,
			   uint32_t           length,
			   uint32_t           style)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)data;
   Ecore_IMF_Preedit_Attr *attr;

   attr = calloc(1, sizeof(*attr));

   switch (style) {
      case TEXT_MODEL_PREEDIT_STYLE_DEFAULT:
      case TEXT_MODEL_PREEDIT_STYLE_UNDERLINE:
      case TEXT_MODEL_PREEDIT_STYLE_INCORRECT:
      case TEXT_MODEL_PREEDIT_STYLE_HIGHLIGHT:
      case TEXT_MODEL_PREEDIT_STYLE_ACTIVE:
      case TEXT_MODEL_PREEDIT_STYLE_INACTIVE:
         attr->preedit_type = ECORE_IMF_PREEDIT_TYPE_SUB1;
         break;
      case TEXT_MODEL_PREEDIT_STYLE_SELECTION:
         attr->preedit_type = ECORE_IMF_PREEDIT_TYPE_SUB2;
         break;
   }

   attr->start_index = index;
   attr->end_index = index + length;

   imcontext->preedit_info.attrs = eina_list_append(imcontext->preedit_info.attrs, attr);
}

static void
text_model_preedit_cursor(void              *data,
			  struct text_model *text_model,
			  uint32_t           serial,
			  int32_t            index)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)data;

   imcontext->preedit_info.cursor = index;
}

static xkb_mod_index_t
modifiers_get_index(struct wl_array *modifiers_map,
                    const char *name)
{
	xkb_mod_index_t index = 0;
	char *p = modifiers_map->data;

	while ((const char *)p < (const char *)(modifiers_map->data + modifiers_map->size)) {
		if (strcmp(p, name) == 0)
			return index;

		index++;
		p += strlen(p) + 1;
	}

	return XKB_MOD_INVALID;
}

static xkb_mod_mask_t
modifiers_get_mask(struct wl_array *modifiers_map,
                   const char *name)
{
	xkb_mod_index_t index = modifiers_get_index(modifiers_map, name);

	if (index == XKB_MOD_INVALID)
		return XKB_MOD_INVALID;

	return 1 << index;
}
static void
text_model_modifiers_map(void *data,
			 struct text_model *text_model,
			 struct wl_array *map)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)data;

   imcontext->shift_mask = modifiers_get_mask(map, "Shift");
   imcontext->control_mask = modifiers_get_mask(map, "Control");
   imcontext->alt_mask = modifiers_get_mask(map, "Mod1");
}

static void
text_model_keysym(void              *data,
                  struct text_model *text_model,
                  uint32_t           serial,
		  uint32_t           time,
                  uint32_t           sym,
                  uint32_t           state,
                  uint32_t           modifiers)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)data;
   char string[32], key[32], keyname[32];
   Ecore_Event_Key *e;

   memset(key, 0, sizeof(key));
   xkb_keysym_get_name(sym, key, sizeof(key));

   memset(keyname, 0, sizeof(keyname));
   xkb_keysym_get_name(sym, keyname, sizeof(keyname));
   if (keyname[0] == '\0')
     snprintf(keyname, sizeof(keyname), "Keysym-%i", key);

   memset(string, 0, sizeof(string));
   xkb_keysym_to_utf8(sym, string, 32);

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom,
                     "key event (key: %s)",
                     keyname);

   e = malloc(sizeof(Ecore_Event_Key) + strlen(key) + strlen(keyname) + strlen(string) + 3);
   if (!e) return;

   e->keyname = (char *)(e + 1);
   e->key = e->keyname + strlen(keyname) + 1;
   e->string = e->key + strlen(key) + 1;
   e->compose = e->string;

   strcpy((char *)e->keyname, keyname);
   strcpy((char *)e->key, key);
   strcpy((char *)e->string, string);

   e->window = imcontext->window->id;
   e->event_window = imcontext->window->id;
   e->timestamp = time;

   e->modifiers = 0;
   if (modifiers & imcontext->shift_mask)
     e->modifiers |= ECORE_EVENT_MODIFIER_SHIFT;
   if (modifiers & imcontext->control_mask)
     e->modifiers |= ECORE_EVENT_MODIFIER_CTRL;
   if (modifiers & imcontext->alt_mask)
     e->modifiers |= ECORE_EVENT_MODIFIER_ALT;

   if (state)
     ecore_event_add(ECORE_EVENT_KEY_DOWN, e, NULL, NULL);
   else
     ecore_event_add(ECORE_EVENT_KEY_UP, e, NULL, NULL);
}

static void
text_model_selection_replacement(void              *data,
                                 struct text_model *text_model)
{
}

static void
text_model_direction(void              *data,
                     struct text_model *text_model)
{
}

static void
text_model_locale(void              *data,
                  struct text_model *text_model)
{
}

static void
text_model_enter(void              *data,
                 struct text_model *text_model,
                 struct wl_surface *surface)
{
}

static void
text_model_leave(void              *data,
                 struct text_model *text_model)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)data;

   /* clear preedit */
   commit_preedit(imcontext);
   clear_preedit(imcontext);

   ecore_imf_context_preedit_changed_event_add(imcontext->ctx);
   ecore_imf_context_event_callback_call(imcontext->ctx, ECORE_IMF_CALLBACK_PREEDIT_CHANGED, NULL);

   ecore_imf_context_preedit_end_event_add(imcontext->ctx);
   ecore_imf_context_event_callback_call(imcontext->ctx, ECORE_IMF_CALLBACK_PREEDIT_END, NULL);
}

static const struct text_model_listener text_model_listener = {
     text_model_commit_string,
     text_model_preedit_string,
     text_model_delete_surrounding_text,
     text_model_preedit_styling,
     text_model_preedit_cursor,
     text_model_modifiers_map,
     text_model_keysym,
     text_model_selection_replacement,
     text_model_direction,
     text_model_locale,
     text_model_enter,
     text_model_leave
};

EAPI void
wayland_im_context_add(Ecore_IMF_Context *ctx)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)ecore_imf_context_data_get(ctx);

   imcontext->ctx = ctx;

   imcontext->text_model = text_model_factory_create_text_model(imcontext->text_model_factory);
   text_model_add_listener(imcontext->text_model, &text_model_listener, imcontext);
}

EAPI void
wayland_im_context_del(Ecore_IMF_Context *ctx)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)ecore_imf_context_data_get(ctx);

   text_model_destroy(imcontext->text_model);

   clear_preedit(imcontext);
}

EAPI void
wayland_im_context_reset (Ecore_IMF_Context *ctx)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)ecore_imf_context_data_get(ctx);

   commit_preedit(imcontext);
   clear_preedit(imcontext);

   text_model_reset(imcontext->text_model, imcontext->serial);
}

EAPI void
wayland_im_context_focus_in(Ecore_IMF_Context *ctx)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)ecore_imf_context_data_get(ctx);

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom, "focus-in");

   if (!imcontext->window)
     return;

   text_model_activate(imcontext->text_model,
                       imcontext->serial,
                       imcontext->window->display->input->seat,
                       ecore_wl_window_surface_get(imcontext->window));
}

EAPI void
wayland_im_context_focus_out(Ecore_IMF_Context *ctx)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)ecore_imf_context_data_get(ctx);

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom, "focus-out");

   if (!imcontext->window)
     return;

   text_model_deactivate(imcontext->text_model,
                         imcontext->window->display->input->seat);
}

EAPI void
wayland_im_context_preedit_string_get(Ecore_IMF_Context  *ctx,
                                      char              **str,
                                      int                *cursor_pos)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)ecore_imf_context_data_get(ctx);

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom,
                     "pre-edit string requested (preedit: `%s')",
                     imcontext->preedit_text ? imcontext->preedit_text : "");

   if (str)
     *str = strdup(imcontext->preedit_text ? imcontext->preedit_text : "");

   if (cursor_pos)
     *cursor_pos = imcontext->preedit_cursor;
}

EAPI void
wayland_im_context_preedit_string_with_attributes_get(Ecore_IMF_Context  *ctx,
                                                      char              **str,
                                                      Eina_List         **attrs,
                                                      int                *cursor_pos)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)ecore_imf_context_data_get(ctx);

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom,
                     "pre-edit string with attributes requested (preedit: `%s')",
                     imcontext->preedit_text ? imcontext->preedit_text : "");

   if (str)
     *str = strdup(imcontext->preedit_text ? imcontext->preedit_text : "");

   if (attrs)
     {
        Eina_List *l;
        Ecore_IMF_Preedit_Attr *a, *attr;

        EINA_LIST_FOREACH(imcontext->preedit_attrs, l, a)
          {
             attr = malloc(sizeof(*attr));
             attr = memcpy(attr, a, sizeof(*attr));
             *attrs = eina_list_append(*attrs, attr);
          }
     }

   if (cursor_pos)
     *cursor_pos = imcontext->preedit_cursor;
}

EAPI void
wayland_im_context_cursor_position_set(Ecore_IMF_Context *ctx,
                                       int                cursor_pos)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)ecore_imf_context_data_get(ctx);
   char *text;

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom,
                     "set cursor position (cursor: %d)",
                     cursor_pos);

   if (ecore_imf_context_surrounding_get(imcontext->ctx, &text, NULL))
     {
        text_model_set_surrounding_text(imcontext->text_model,
                                        text,
                                        cursor_pos,
                                        cursor_pos);
        free(text);
     }
}

EAPI void
wayland_im_context_use_preedit_set(Ecore_IMF_Context *ctx,
                                   Eina_Bool          use_preedit)
{
}

EAPI void
wayland_im_context_client_window_set(Ecore_IMF_Context *ctx,
                                     void              *window)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)ecore_imf_context_data_get(ctx);

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom, "client window set (window: %p)", window);

   if (window != NULL)
     imcontext->window = ecore_wl_window_find((Ecore_Window)window);
}

EAPI void
wayland_im_context_client_canvas_set(Ecore_IMF_Context *ctx,
                                     void              *canvas)
{
   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom, "client canvas set");
}

EAPI Eina_Bool
wayland_im_context_filter_event(Ecore_IMF_Context    *ctx,
                                Ecore_IMF_Event_Type  type,
                                Ecore_IMF_Event      *event)
{
   return EINA_FALSE;
}

WaylandIMContext *wayland_im_context_new (struct text_model_factory *text_model_factory)
{
   WaylandIMContext *context = calloc(1, sizeof(WaylandIMContext));

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom, "new context created");
   context->text_model_factory = text_model_factory;

   return context;
}

/* vim:ts=8 sw=3 sts=3 expandtab cino=>5n-3f0^-2{2(0W1st0
 */
