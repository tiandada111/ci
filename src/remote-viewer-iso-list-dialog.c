/*
 * Virt Viewer: A virtual machine console viewer
 *
 * Copyright (C) 2017 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>

#include <glib/gi18n.h>

#include "glib-compat.h"
#include "remote-viewer-iso-list-dialog.h"
#include "virt-viewer-util.h"
#include "ovirt-foreign-menu.h"

static void ovirt_foreign_menu_iso_name_changed(OvirtForeignMenu *foreign_menu, GAsyncResult *result, RemoteViewerISOListDialog *self);
static void remote_viewer_iso_list_dialog_show_error(RemoteViewerISOListDialog *self, const gchar *message);

struct _RemoteViewerISOListDialog
{
    GtkDialog parent;
    GtkHeaderBar *header_bar;
    GtkListStore *list_store;
    GtkWidget *status;
    GtkWidget *spinner;
    GtkWidget *stack;
    GtkWidget *tree_view;
    OvirtForeignMenu *foreign_menu;
    GCancellable *cancellable;
};

G_DEFINE_TYPE(RemoteViewerISOListDialog, remote_viewer_iso_list_dialog, GTK_TYPE_DIALOG)

enum RemoteViewerISOListDialogModel
{
    ISO_IS_ACTIVE = 0,
    ISO_NAME,
    FONT_WEIGHT,
    ISO_ID,
};

enum RemoteViewerISOListDialogProperties {
    PROP_0,
    PROP_FOREIGN_MENU,
};


void remote_viewer_iso_list_dialog_toggled(GtkCellRendererToggle *cell_renderer, gchar *path, gpointer user_data);
void remote_viewer_iso_list_dialog_row_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col, gpointer user_data);

static void
remote_viewer_iso_list_dialog_dispose(GObject *object)
{
    RemoteViewerISOListDialog *self = REMOTE_VIEWER_ISO_LIST_DIALOG(object);

    g_clear_object(&self->cancellable);

    if (self->foreign_menu) {
        g_signal_handlers_disconnect_by_data(self->foreign_menu, object);
        g_clear_object(&self->foreign_menu);
    }
    G_OBJECT_CLASS(remote_viewer_iso_list_dialog_parent_class)->dispose(object);
}

static void
remote_viewer_iso_list_dialog_set_property(GObject *object, guint property_id,
                                           const GValue *value, GParamSpec *pspec)
{
    RemoteViewerISOListDialog *self = REMOTE_VIEWER_ISO_LIST_DIALOG(object);

    switch (property_id) {
    case PROP_FOREIGN_MENU:
        self->foreign_menu = g_value_dup_object(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
remote_viewer_iso_list_dialog_class_init(RemoteViewerISOListDialogClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);

    object_class->dispose = remote_viewer_iso_list_dialog_dispose;
    object_class->set_property = remote_viewer_iso_list_dialog_set_property;

    g_object_class_install_property(object_class,
                                    PROP_FOREIGN_MENU,
                                    g_param_spec_object("foreign-menu",
                                                        "oVirt Foreign Menu",
                                                        "Object which is used as interface to oVirt",
                                                        OVIRT_TYPE_FOREIGN_MENU,
                                                        G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
remote_viewer_iso_list_dialog_show_files(RemoteViewerISOListDialog *self)
{
    gtk_stack_set_visible_child_full(GTK_STACK(self->stack), "iso-list",
                                     GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(self), GTK_RESPONSE_NONE, TRUE);
}

static void
remote_viewer_iso_list_dialog_set_subtitle(RemoteViewerISOListDialog *self, const char *iso_name)
{
    gchar *subtitle = NULL;

    if (iso_name && strlen(iso_name) != 0)
        subtitle = g_strdup_printf(_("Current: %s"), iso_name);

    gtk_header_bar_set_subtitle(self->header_bar, subtitle);
    g_free(subtitle);
}

static void
remote_viewer_iso_list_dialog_foreach(GStrv info, RemoteViewerISOListDialog *self)
{
    GStrv current_iso = ovirt_foreign_menu_get_current_iso_info(self->foreign_menu);
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    gboolean active = (g_strv_equal((const gchar * const *) current_iso,
                                    (const gchar * const *) info) == TRUE);
G_GNUC_END_IGNORE_DEPRECATIONS
    gint weight = active ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL;
    GtkTreeIter iter;

    gtk_list_store_append(self->list_store, &iter);
    gtk_list_store_set(self->list_store, &iter,
                       ISO_IS_ACTIVE, active,
                       ISO_NAME, info[0],
                       FONT_WEIGHT, weight,
                       ISO_ID, info[1],
                       -1);

    if (active) {
        GtkTreePath *path = gtk_tree_model_get_path(GTK_TREE_MODEL(self->list_store), &iter);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(self->tree_view), path, NULL, FALSE);
        gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(self->tree_view), path, NULL, TRUE, 0.5, 0.5);
        gtk_tree_path_free(path);
        remote_viewer_iso_list_dialog_set_subtitle(self, current_iso[0]);
    }
}

static void
fetch_iso_names_cb(OvirtForeignMenu *foreign_menu,
                   GAsyncResult *result,
                   RemoteViewerISOListDialog *self)
{
    GError *error = NULL;
    GList *iso_list;

    iso_list = ovirt_foreign_menu_fetch_iso_names_finish(foreign_menu, result, &error);

    if (!iso_list) {
        const gchar *msg = error ? error->message : _("No ISO files in domain");
        gchar *markup;

        g_debug("Error fetching ISO names: %s", msg);
        if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            goto end;

        markup = g_markup_printf_escaped("<b>%s</b>", msg);
        gtk_label_set_markup(GTK_LABEL(self->status), markup);
        gtk_spinner_stop(GTK_SPINNER(self->spinner));
        remote_viewer_iso_list_dialog_show_error(self, msg);
        gtk_dialog_set_response_sensitive(GTK_DIALOG(self), GTK_RESPONSE_NONE, TRUE);
        g_free(markup);
        goto end;
    }

    g_clear_object(&self->cancellable);
    g_list_foreach(iso_list, (GFunc) remote_viewer_iso_list_dialog_foreach, self);
    remote_viewer_iso_list_dialog_show_files(self);

end:
    g_clear_error(&error);
}


static void
remote_viewer_iso_list_dialog_refresh_iso_list(RemoteViewerISOListDialog *self)
{
    gtk_list_store_clear(self->list_store);

    self->cancellable = g_cancellable_new();
    ovirt_foreign_menu_fetch_iso_names_async(self->foreign_menu,
                                             self->cancellable,
                                             (GAsyncReadyCallback) fetch_iso_names_cb,
                                             self);
}

static void
remote_viewer_iso_list_dialog_response(GtkDialog *dialog,
                                       gint response_id,
                                       gpointer user_data G_GNUC_UNUSED)
{
    RemoteViewerISOListDialog *self = REMOTE_VIEWER_ISO_LIST_DIALOG(dialog);

    if (response_id != GTK_RESPONSE_NONE) {
        g_cancellable_cancel(self->cancellable);
        return;
    }

    gtk_spinner_start(GTK_SPINNER(self->spinner));
    gtk_label_set_markup(GTK_LABEL(self->status), _("<b>Loading...</b>"));
    remote_viewer_iso_list_dialog_set_subtitle(self, NULL);
    gtk_stack_set_visible_child_full(GTK_STACK(self->stack), "status",
                                     GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(self), GTK_RESPONSE_NONE, FALSE);
    remote_viewer_iso_list_dialog_refresh_iso_list(self);
}

G_MODULE_EXPORT void
remote_viewer_iso_list_dialog_toggled(GtkCellRendererToggle *cell_renderer G_GNUC_UNUSED,
                                      gchar *path,
                                      gpointer user_data)
{
    RemoteViewerISOListDialog *self = REMOTE_VIEWER_ISO_LIST_DIALOG(user_data);
    GtkTreeModel *model = GTK_TREE_MODEL(self->list_store);
    GtkTreePath *tree_path = gtk_tree_path_new_from_string(path);
    GtkTreeIter iter;
    gboolean active;
    gchar *name, *id;

    gtk_tree_view_set_cursor(GTK_TREE_VIEW(self->tree_view), tree_path, NULL, FALSE);
    gtk_tree_model_get_iter(model, &iter, tree_path);
    gtk_tree_model_get(model, &iter,
                       ISO_IS_ACTIVE, &active,
                       ISO_NAME, &name,
                       ISO_ID, &id,
                       -1);

    gtk_dialog_set_response_sensitive(GTK_DIALOG(self), GTK_RESPONSE_NONE, FALSE);
    gtk_widget_set_sensitive(self->tree_view, FALSE);

    self->cancellable = g_cancellable_new();
    ovirt_foreign_menu_set_current_iso_name_async(self->foreign_menu,
                                                  active ? NULL : name,
                                                  active ? NULL : id,
                                                  self->cancellable,
                                                  (GAsyncReadyCallback)ovirt_foreign_menu_iso_name_changed,
                                                  self);
    gtk_tree_path_free(tree_path);
    g_free(name);
    g_free(id);
}

G_MODULE_EXPORT void
remote_viewer_iso_list_dialog_row_activated(GtkTreeView *view G_GNUC_UNUSED,
                                            GtkTreePath *path,
                                            GtkTreeViewColumn *col G_GNUC_UNUSED,
                                            gpointer user_data)
{
    gchar *path_str = gtk_tree_path_to_string(path);
    remote_viewer_iso_list_dialog_toggled(NULL, path_str, user_data);
    g_free(path_str);
}

static void
remote_viewer_iso_list_dialog_init(RemoteViewerISOListDialog *self)
{
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(self));
    GtkBuilder *builder = virt_viewer_util_load_ui("remote-viewer-iso-list.ui");
    GtkCellRendererToggle *cell_renderer;
    GtkWidget *button, *image;

    gtk_builder_connect_signals(builder, self);

    self->header_bar = GTK_HEADER_BAR(gtk_dialog_get_header_bar(GTK_DIALOG(self)));
    gtk_header_bar_set_has_subtitle(self->header_bar, TRUE);

    self->status = GTK_WIDGET(gtk_builder_get_object(builder, "status"));
    self->spinner = GTK_WIDGET(gtk_builder_get_object(builder, "spinner"));
    self->stack = GTK_WIDGET(gtk_builder_get_object(builder, "stack"));
    gtk_box_pack_start(GTK_BOX(content), self->stack, TRUE, TRUE, 0);

    self->list_store = GTK_LIST_STORE(gtk_builder_get_object(builder, "liststore"));
    self->tree_view = GTK_WIDGET(gtk_builder_get_object(builder, "view"));
    cell_renderer = GTK_CELL_RENDERER_TOGGLE(gtk_builder_get_object(builder, "cellrenderertoggle"));
    gtk_cell_renderer_toggle_set_radio(cell_renderer, TRUE);
    gtk_cell_renderer_set_padding(GTK_CELL_RENDERER(cell_renderer), 6, 6);

    g_object_unref(builder);

    button = gtk_dialog_add_button(GTK_DIALOG(self), "", GTK_RESPONSE_NONE);
    image = gtk_image_new_from_icon_name("view-refresh-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_button_set_image(GTK_BUTTON(button), image);
    gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
    gtk_widget_set_tooltip_text(button, _("Refresh"));

    gtk_dialog_set_response_sensitive(GTK_DIALOG(self), GTK_RESPONSE_NONE, FALSE);
    g_signal_connect(self, "response", G_CALLBACK(remote_viewer_iso_list_dialog_response), NULL);
}

static void
remote_viewer_iso_list_dialog_show_error(RemoteViewerISOListDialog *self,
                                         const gchar *message)
{
    GtkWidget *dialog;

    g_warn_if_fail(message != NULL);

    dialog = gtk_message_dialog_new(GTK_WINDOW(self),
                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                    GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_CLOSE,
                                    "%s", message ? message : _("Unspecified error"));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void
ovirt_foreign_menu_iso_name_changed(OvirtForeignMenu *foreign_menu,
                                    GAsyncResult *result,
                                    RemoteViewerISOListDialog *self)
{
    GtkTreeModel *model = GTK_TREE_MODEL(self->list_store);
    GStrv current_iso;
    GtkTreeIter iter;
    gchar *name, *id;
    gboolean active, match = FALSE;
    GError *error = NULL;

    /* In the case of error, don't return early, because it is necessary to
     * change the ISO back to the original, avoiding a possible inconsistency.
     */
    if (!ovirt_foreign_menu_set_current_iso_name_finish(foreign_menu, result, &error)) {
        const gchar *msg = error ? error->message : _("Failed to change CD");
        g_debug("Error changing ISO: %s", msg);

        if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            goto end;

        remote_viewer_iso_list_dialog_show_error(self, msg);
    }

    g_clear_object(&self->cancellable);
    if (!gtk_tree_model_get_iter_first(model, &iter))
        goto end;

    current_iso = ovirt_foreign_menu_get_current_iso_info(foreign_menu);

    do {
        gtk_tree_model_get(model, &iter,
                           ISO_IS_ACTIVE, &active,
                           ISO_NAME, &name,
                           ISO_ID, &id,
                           -1);

        if (current_iso)
            match = (g_strcmp0(current_iso[0], name) == 0 &&
                     g_strcmp0(current_iso[1], id) == 0);

        /* iso is not active anymore */
        if (active && !match) {
            gtk_list_store_set(self->list_store, &iter,
                               ISO_IS_ACTIVE, FALSE,
                               FONT_WEIGHT, PANGO_WEIGHT_NORMAL, -1);
        } else if (match) {
            gtk_list_store_set(self->list_store, &iter,
                               ISO_IS_ACTIVE, TRUE,
                               FONT_WEIGHT, PANGO_WEIGHT_BOLD, -1);
        }

        g_free(name);
        g_free(id);
    } while (gtk_tree_model_iter_next(model, &iter));

    remote_viewer_iso_list_dialog_set_subtitle(self, current_iso ? current_iso[0] : NULL);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(self), GTK_RESPONSE_NONE, TRUE);
    gtk_widget_set_sensitive(self->tree_view, TRUE);

end:
    g_clear_error(&error);
}

GtkWidget *
remote_viewer_iso_list_dialog_new(GtkWindow *parent, GObject *foreign_menu)
{
    GtkWidget *dialog;
    RemoteViewerISOListDialog *self;

    g_return_val_if_fail(foreign_menu != NULL, NULL);

    dialog = g_object_new(REMOTE_VIEWER_TYPE_ISO_LIST_DIALOG,
                          "title", _("Change CD"),
                          "transient-for", parent,
                          "border-width", 18,
                          "default-width", 400,
                          "default-height", 300,
                          "use-header-bar", TRUE,
                          "foreign-menu", foreign_menu,
                          NULL);

    self = REMOTE_VIEWER_ISO_LIST_DIALOG(dialog);
    remote_viewer_iso_list_dialog_refresh_iso_list(self);
    return dialog;
}
