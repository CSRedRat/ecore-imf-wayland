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
   int preedit_cursor_pos;
};

static void
text_model_commit_string(void              *data,
                         struct text_model *text_model,
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
text_model_preedit_string(void              *data,
                          struct text_model *text_model,
                          const char        *text,
                          uint32_t           index)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)data;

   Eina_Bool old_preedit = EINA_FALSE;

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom,
                     "preedit event (text: `%s' index: %u, current pre-edit: `%s' index: %d)",
                     text, index,
                     imcontext->preedit_text ? imcontext->preedit_text : "",
                     imcontext->preedit_cursor_pos);

    if (imcontext->preedit_text)
     {
        old_preedit = strlen(imcontext->preedit_text) > 0;
        free(imcontext->preedit_text);
     }

   imcontext->preedit_text = strdup(text);
   imcontext->preedit_cursor_pos = index;

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
text_model_preedit_styling(void *data,
                           struct text_model *text_model)
{
}

static void
text_model_key(void              *data,
               struct text_model *text_model,
               uint32_t           sym,
               uint32_t           state)
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
   e->timestamp = -1; /* TODO add timestamp to protocol */
   e->modifiers = 0; /* TODO add modifiers to protocol */

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
   imcontext->preedit_cursor_pos = 0;
   free(imcontext->preedit_text);
   imcontext->preedit_text = NULL;

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
     text_model_key,
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

   free(imcontext->preedit_text);
   imcontext->preedit_text = NULL;
}

EAPI void
wayland_im_context_reset (Ecore_IMF_Context *ctx)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)ecore_imf_context_data_get(ctx);

   free(imcontext->preedit_text);
   imcontext->preedit_text = NULL;

   imcontext->preedit_cursor_pos = 0;

   text_model_reset(imcontext->text_model);
}

EAPI void
wayland_im_context_focus_in(Ecore_IMF_Context *ctx)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)ecore_imf_context_data_get(ctx);

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom, "focus-in");

   if (!imcontext->window)
     return;

   text_model_activate(imcontext->text_model,
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
     *cursor_pos = imcontext->preedit_cursor_pos;
}

EAPI void
wayland_im_context_preedit_string_with_attributes_get(Ecore_IMF_Context  *ctx,
                                                      char              **str,
                                                      Eina_List         **attr,
                                                      int                *cursor_pos)
{
   WaylandIMContext *imcontext = (WaylandIMContext *)ecore_imf_context_data_get(ctx);

   EINA_LOG_DOM_INFO(_ecore_imf_wayland_log_dom,
                     "pre-edit string with attributes requested (preedit: `%s')",
                     imcontext->preedit_text ? imcontext->preedit_text : "");

   if (str)
     *str = strdup(imcontext->preedit_text ? imcontext->preedit_text : "");

   if (attr)
     *attr = NULL;

   if (cursor_pos)
     *cursor_pos = imcontext->preedit_cursor_pos;
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
