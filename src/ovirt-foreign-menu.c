/*
 * Virt Viewer: A virtual machine console viewer
 *
 * Copyright (C) 2007-2014 Red Hat, Inc.
 * Copyright (C) 2009-2012 Daniel P. Berrange
 * Copyright (C) 2010 Marc-André Lureau
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
 *
 * Author: Daniel P. Berrange <berrange@redhat.com>
 *         Christope Fergeau <cfergeau@redhat.com>
 */

#include <config.h>

#include <string.h>

#include "ovirt-foreign-menu.h"
#include "virt-viewer-util.h"
#include "glib-compat.h"

/* GLib 2.69 annotated macros with version tags, and
 * since we set GLIB_VERSION_MAX_ALLOWED  to 2.48
 * it complains if we use G_GNUC_FALLTHROUGH at
 * all. We temporarily purge the GLib definition
 * of G_GNUC_FALLTHROUGH and define it ourselves.
 * When we set min glib >= 2.60, we can delete
 * all this
 */
#ifndef __GNUC_PREREQ
# define __GNUC_PREREQ(maj, min) \
    ((__GNUC__ << 16) + __GNUC_MINOR__ >= ((maj) << 16) + (min))
#endif
#undef G_GNUC_FALLTHROUGH
#if __GNUC_PREREQ (7, 0)
# define G_GNUC_FALLTHROUGH __attribute__((fallthrough))
#else
# define G_GNUC_FALLTHROUGH do {} while(0)
#endif

typedef enum {
    STATE_0,
    STATE_API,
    STATE_VM,
    STATE_HOST,
    STATE_CLUSTER,
    STATE_DATA_CENTER,
    STATE_STORAGE_DOMAIN,
    STATE_VM_CDROM,
    STATE_CDROM_FILE,
    STATE_ISOS
} OvirtForeignMenuState;

static void ovirt_foreign_menu_next_async_step(OvirtForeignMenu *menu, GTask *task, OvirtForeignMenuState state);
static void ovirt_foreign_menu_fetch_api_async(OvirtForeignMenu *menu, GTask *task);
static void ovirt_foreign_menu_fetch_vm_async(OvirtForeignMenu *menu, GTask *task);
static void ovirt_foreign_menu_fetch_host_async(OvirtForeignMenu *menu, GTask *task);
static void ovirt_foreign_menu_fetch_cluster_async(OvirtForeignMenu *menu, GTask *task);
static void ovirt_foreign_menu_fetch_data_center_async(OvirtForeignMenu *menu, GTask *task);
static void ovirt_foreign_menu_fetch_storage_domain_async(OvirtForeignMenu *menu, GTask *task);
static void ovirt_foreign_menu_fetch_vm_cdrom_async(OvirtForeignMenu *menu, GTask *task);
static void ovirt_foreign_menu_refresh_cdrom_file_async(OvirtForeignMenu *menu, GTask *task);
static void ovirt_foreign_menu_fetch_iso_list_async(OvirtForeignMenu *menu, GTask *task);


struct _OvirtForeignMenu {
    GObject parent;
    OvirtProxy *proxy;
    OvirtApi *api;
    OvirtVm *vm;
    OvirtHost *host;
    OvirtCluster *cluster;
    OvirtDataCenter *data_center;
    char *vm_guid;

    OvirtCollection *files;
    OvirtCdrom *cdrom;

    /* The next 2 members are used when changing the ISO image shown in
     * a VM */
    /* Name of the ISO which is currently used by the VM OvirtCdrom */
    GStrv current_iso_info;
    /* Name of the ISO we are trying to insert in the VM OvirtCdrom */
    GStrv next_iso_info;

    GList *iso_names;
};


G_DEFINE_TYPE(OvirtForeignMenu, ovirt_foreign_menu, G_TYPE_OBJECT)


enum {
    PROP_0,
    PROP_PROXY,
    PROP_API,
    PROP_VM,
    PROP_FILE,
    PROP_FILES,
    PROP_VM_GUID,
};

gchar *
ovirt_foreign_menu_get_current_iso_name(OvirtForeignMenu *foreign_menu)
{
    gchar *name;

    if (foreign_menu->cdrom == NULL) {
        return NULL;
    }

    g_object_get(foreign_menu->cdrom, "file", &name, NULL);

    return name;
}

static GStrv
iso_info_new(const gchar *name, const gchar *id)
{
    GStrv info = g_new0(gchar *, 3);
    info[0] = g_strdup(name);
    info[1] = id != NULL ? g_strdup(id) : g_strdup(name);
    return info;
}


GStrv
ovirt_foreign_menu_get_current_iso_info(OvirtForeignMenu *menu)
{
    if (menu->cdrom == NULL)
        return NULL;

    return menu->current_iso_info;
}

static void
ovirt_foreign_menu_set_current_iso_info(OvirtForeignMenu *menu, const gchar *name, const gchar *id)
{
    GStrv info = NULL;

    g_debug("Setting current ISO to: name '%s', id '%s'", name, id);
    if (menu->cdrom == NULL)
        return;

    if (name != NULL)
        info = iso_info_new(name, id);

    g_strfreev(menu->current_iso_info);
    menu->current_iso_info = info;
}

GList*
ovirt_foreign_menu_get_iso_names(OvirtForeignMenu *foreign_menu)
{
    return foreign_menu->iso_names;
}


static void
ovirt_foreign_menu_get_property(GObject *object, guint property_id,
                                       GValue *value, GParamSpec *pspec)
{
    OvirtForeignMenu *self = OVIRT_FOREIGN_MENU(object);

    switch (property_id) {
    case PROP_PROXY:
        g_value_set_object(value, self->proxy);
        break;
    case PROP_API:
        g_value_set_object(value, self->api);
        break;
    case PROP_VM:
        g_value_set_object(value, self->vm);
        break;
    case PROP_FILE:
        g_value_take_string(value,
                            ovirt_foreign_menu_get_current_iso_name(self));
        break;
    case PROP_FILES:
        g_value_set_pointer(value, self->iso_names);
        break;
    case PROP_VM_GUID:
        g_value_set_string(value, self->vm_guid);
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}


static void
ovirt_foreign_menu_set_property(GObject *object, guint property_id,
                                       const GValue *value G_GNUC_UNUSED, GParamSpec *pspec)
{
    OvirtForeignMenu *self = OVIRT_FOREIGN_MENU(object);

    switch (property_id) {
    case PROP_PROXY:
        g_clear_object(&self->proxy);
        self->proxy = g_value_dup_object(value);
        break;
    case PROP_API:
        g_clear_object(&self->api);
        self->api = g_value_dup_object(value);
        break;
    case PROP_VM:
        g_clear_object(&self->vm);
        self->vm = g_value_dup_object(value);
        g_clear_pointer(&self->vm_guid, g_free);
        if (self->vm != NULL) {
            g_object_get(G_OBJECT(self->vm), "guid", &self->vm_guid, NULL);
        }
        break;
    case PROP_VM_GUID:
        g_clear_object(&self->vm);
        g_free(self->vm_guid);
        self->vm_guid = g_value_dup_string(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}


static void
ovirt_foreign_menu_dispose(GObject *obj)
{
    OvirtForeignMenu *self = OVIRT_FOREIGN_MENU(obj);

    g_clear_object(&self->proxy);
    g_clear_object(&self->api);
    g_clear_object(&self->vm);
    g_clear_object(&self->host);
    g_clear_object(&self->cluster);
    g_clear_object(&self->data_center);
    g_clear_pointer(&self->vm_guid, g_free);
    g_clear_object(&self->files);
    g_clear_object(&self->cdrom);

    if (self->iso_names) {
        g_list_free_full(self->iso_names, (GDestroyNotify)g_free);
        self->iso_names = NULL;
    }

    g_clear_pointer(&self->current_iso_info, g_strfreev);
    g_clear_pointer(&self->next_iso_info, g_strfreev);

    G_OBJECT_CLASS(ovirt_foreign_menu_parent_class)->dispose(obj);
}


static void
ovirt_foreign_menu_class_init(OvirtForeignMenuClass *klass)
{
    GObjectClass *oclass = G_OBJECT_CLASS(klass);

    oclass->get_property = ovirt_foreign_menu_get_property;
    oclass->set_property = ovirt_foreign_menu_set_property;
    oclass->dispose = ovirt_foreign_menu_dispose;

    g_object_class_install_property(oclass,
                                    PROP_PROXY,
                                    g_param_spec_object("proxy",
                                                        "OvirtProxy instance",
                                                        "OvirtProxy instance",
                                                        OVIRT_TYPE_PROXY,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_CONSTRUCT_ONLY |
                                                        G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(oclass,
                                    PROP_API,
                                    g_param_spec_object("api",
                                                        "OvirtApi instance",
                                                        "Ovirt api root",
                                                        OVIRT_TYPE_API,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(oclass,
                                    PROP_VM,
                                    g_param_spec_object("vm",
                                                        "OvirtVm instance",
                                                        "OvirtVm being handled",
                                                        OVIRT_TYPE_VM,
                                                        G_PARAM_READWRITE |
                                                        G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(oclass,
                                    PROP_FILE,
                                    g_param_spec_string("file",
                                                         "File",
                                                         "Name of the image currently inserted in the virtual CDROM",
                                                         NULL,
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(oclass,
                                    PROP_FILES,
                                    g_param_spec_pointer("files",
                                                         "ISO names",
                                                         "GSList of ISO names for this oVirt VM",
                                                         G_PARAM_READABLE |
                                                         G_PARAM_STATIC_STRINGS));
    g_object_class_install_property(oclass,
                                    PROP_VM_GUID,
                                    g_param_spec_string("vm-guid",
                                                         "VM GUID",
                                                         "GUID of the virtual machine to provide a foreign menu for",
                                                         NULL,
                                                         G_PARAM_READWRITE |
                                                         G_PARAM_CONSTRUCT_ONLY |
                                                         G_PARAM_STATIC_STRINGS));
}


static void
ovirt_foreign_menu_init(OvirtForeignMenu *self G_GNUC_UNUSED)
{
}


OvirtForeignMenu* ovirt_foreign_menu_new(OvirtProxy *proxy)
{
    return g_object_new(OVIRT_TYPE_FOREIGN_MENU,
                        "proxy", proxy,
                        NULL);
}


static void
ovirt_foreign_menu_next_async_step(OvirtForeignMenu *menu,
                                   GTask *task,
                                   OvirtForeignMenuState current_state)
{
    /* Each state will check if the member is initialized, falling directly to
     * the next one if so. If not, the callback for the asynchronous call will
     * be responsible for calling is function again with the next state as
     * argument.
     */
    switch (current_state + 1) {
    case STATE_API:
        if (menu->api == NULL) {
            ovirt_foreign_menu_fetch_api_async(menu, task);
            break;
        }
        G_GNUC_FALLTHROUGH;
    case STATE_VM:
        if (menu->vm == NULL) {
            ovirt_foreign_menu_fetch_vm_async(menu, task);
            break;
        }
        G_GNUC_FALLTHROUGH;
    case STATE_HOST:
        if (menu->host == NULL) {
            ovirt_foreign_menu_fetch_host_async(menu, task);
            break;
        }
        G_GNUC_FALLTHROUGH;
    case STATE_CLUSTER:
        if (menu->cluster == NULL) {
            ovirt_foreign_menu_fetch_cluster_async(menu, task);
            break;
        }
        G_GNUC_FALLTHROUGH;
    case STATE_DATA_CENTER:
        if (menu->data_center == NULL) {
            ovirt_foreign_menu_fetch_data_center_async(menu, task);
            break;
        }
        G_GNUC_FALLTHROUGH;
    case STATE_STORAGE_DOMAIN:
        if (menu->files == NULL) {
            ovirt_foreign_menu_fetch_storage_domain_async(menu, task);
            break;
        }
        G_GNUC_FALLTHROUGH;
    case STATE_VM_CDROM:
        if (menu->cdrom == NULL) {
            ovirt_foreign_menu_fetch_vm_cdrom_async(menu, task);
            break;
        }
        G_GNUC_FALLTHROUGH;
    case STATE_CDROM_FILE:
        ovirt_foreign_menu_refresh_cdrom_file_async(menu, task);
        break;
    case STATE_ISOS:
        g_warn_if_fail(menu->api != NULL);
        g_warn_if_fail(menu->vm != NULL);
        g_warn_if_fail(menu->files != NULL);
        g_warn_if_fail(menu->cdrom != NULL);

        ovirt_foreign_menu_fetch_iso_list_async(menu, task);
        break;
    default:
        g_warn_if_reached();
        g_task_return_new_error(task, OVIRT_ERROR, OVIRT_ERROR_FAILED,
                                "Invalid state: %u", current_state);
        g_object_unref(task);
    }
}


void
ovirt_foreign_menu_fetch_iso_names_async(OvirtForeignMenu *menu,
                                         GCancellable *cancellable,
                                         GAsyncReadyCallback callback,
                                         gpointer user_data)
{
    GTask *task = g_task_new(menu, cancellable, callback, user_data);
    ovirt_foreign_menu_next_async_step(menu, task, STATE_0);
}


GList *
ovirt_foreign_menu_fetch_iso_names_finish(OvirtForeignMenu *foreign_menu,
                                          GAsyncResult *result,
                                          GError **error)
{
    g_return_val_if_fail(OVIRT_IS_FOREIGN_MENU(foreign_menu), NULL);
    return g_task_propagate_pointer(G_TASK(result), error);
}


static void iso_name_set_cb(GObject *source_object,
                            GAsyncResult *result,
                            gpointer user_data)
{
    GError *error = NULL;
    GTask *task = G_TASK(user_data);
    OvirtForeignMenu *foreign_menu = OVIRT_FOREIGN_MENU(g_task_get_source_object(task));
    gboolean updated;

    updated = ovirt_cdrom_update_finish(OVIRT_CDROM(source_object),
                                        result, &error);
    if (updated) {
        g_debug("Finished updating cdrom content");
        g_strfreev(foreign_menu->current_iso_info);
        foreign_menu->current_iso_info = foreign_menu->next_iso_info;
        foreign_menu->next_iso_info = NULL;
        g_task_return_boolean(task, TRUE);
        goto end;
    }

    /* Reset old state back as we were not successful in switching to
     * the new ISO */
    g_debug("setting OvirtCdrom:file back");
    g_object_set(foreign_menu->cdrom, "file",
                 foreign_menu->current_iso_info ? foreign_menu->current_iso_info[1] : NULL,
                 NULL);
    g_clear_pointer(&foreign_menu->next_iso_info, g_strfreev);

    if (error != NULL) {
        g_warning("failed to update cdrom resource: %s", error->message);
        g_task_return_error(task, error);
    } else {
        g_warn_if_reached();
        g_task_return_new_error(task, OVIRT_ERROR, OVIRT_ERROR_FAILED,
                                "failed to update cdrom resource");
    }

end:
    g_object_unref(task);
}


void ovirt_foreign_menu_set_current_iso_name_async(OvirtForeignMenu *foreign_menu,
                                                   const char *name,
                                                   const char *id,
                                                   GCancellable *cancellable,
                                                   GAsyncReadyCallback callback,
                                                   gpointer user_data)
{
    GTask *task;

    g_return_if_fail(foreign_menu->cdrom != NULL);
    g_return_if_fail(foreign_menu->next_iso_info == NULL);

    if (name) {
        g_debug("Updating VM cdrom image to '%s'", name);
        foreign_menu->next_iso_info = iso_info_new(name, id);
    } else {
        g_debug("Removing current cdrom image");
        foreign_menu->next_iso_info = NULL;
    }

    g_object_set(foreign_menu->cdrom,
                 "file", id,
                 NULL);

    task = g_task_new(foreign_menu, cancellable, callback, user_data);
    ovirt_cdrom_update_async(foreign_menu->cdrom, TRUE,
                             foreign_menu->proxy, cancellable,
                             iso_name_set_cb, task);
}


gboolean ovirt_foreign_menu_set_current_iso_name_finish(OvirtForeignMenu *foreign_menu,
                                                        GAsyncResult *result,
                                                        GError **error)
{
    g_return_val_if_fail(OVIRT_IS_FOREIGN_MENU(foreign_menu), FALSE);
    return g_task_propagate_boolean(G_TASK(result), error);
}


static void ovirt_foreign_menu_set_files(OvirtForeignMenu *menu,
                                         const GList *files)
{
    GList *sorted_files = NULL;
    const GList *it;
    GList *it2;
    gchar *current_iso_name = ovirt_foreign_menu_get_current_iso_name(menu);

    for (it = files; it != NULL; it = it->next) {
        char *name = NULL, *id = NULL;
        g_object_get(it->data, "name", &name, "guid", &id, NULL);

#ifdef HAVE_OVIRT_STORAGE_DOMAIN_GET_DISKS
        if (OVIRT_IS_DISK(it->data)) {
            OvirtDiskContentType content_type;
            g_object_get(it->data, "content-type", &content_type, NULL);
            if (content_type != OVIRT_DISK_CONTENT_TYPE_ISO) {
                g_debug("Ignoring %s disk which content-type is not ISO", name);
                goto loop_end;
            }
        }
#endif

        /* The oVirt REST API is supposed to have a 'type' node
         * associated with file resources , but as of 3.2, this node
         * is not present, so we do an extension check instead
         * to differentiate between ISOs and floppy images */
        if (!g_str_has_suffix(name, ".iso")) {
            g_debug("Ignoring %s which does not have a .iso extension", name);
            goto loop_end;
        }

        g_debug("Adding ISO to the list: name '%s', id '%s'", name, id);
        sorted_files = g_list_insert_sorted(sorted_files, iso_info_new(name, id),
                                            (GCompareFunc)g_strcmp0);

        /* Check if info matches with current cdrom file */
        if (current_iso_name != NULL &&
            (g_strcmp0(current_iso_name, name) == 0 ||
             g_strcmp0(current_iso_name, id) == 0)) {
                ovirt_foreign_menu_set_current_iso_info(menu, name, id);
        }

loop_end:
        g_free(name);
        g_free(id);
    }

    g_free(current_iso_name);

    for (it = sorted_files, it2 = menu->iso_names;
         (it != NULL) && (it2 != NULL);
         it = it->next, it2 = it2->next) {
        if (g_strcmp0(it->data, it2->data) != 0) {
            break;
        }
    }

    if ((it == NULL) && (it2 == NULL)) {
        /* sorted_files and menu->files content was the same */
        g_list_free_full(sorted_files, (GDestroyNotify)g_strfreev);
        return;
    }

    g_list_free_full(menu->iso_names, (GDestroyNotify)g_strfreev);
    menu->iso_names = sorted_files;
}


static void cdrom_file_refreshed_cb(GObject *source_object,
                                    GAsyncResult *result,
                                    gpointer user_data)
{
    GTask *task = G_TASK(user_data);
    OvirtForeignMenu *menu = OVIRT_FOREIGN_MENU(g_task_get_source_object(task));
    OvirtResource *cdrom  = OVIRT_RESOURCE(source_object);
    GError *error = NULL;

    ovirt_resource_refresh_finish(cdrom, result, &error);
    if (error != NULL) {
        g_warning("failed to refresh cdrom content: %s", error->message);
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    /* Content of OvirtCdrom is now current */
    if (menu->cdrom != NULL) {
        ovirt_foreign_menu_next_async_step(menu, task, STATE_CDROM_FILE);
    } else {
        g_debug("Could not find VM cdrom through oVirt REST API");
        g_task_return_new_error(task, OVIRT_ERROR, OVIRT_ERROR_FAILED,
                                "Could not find VM cdrom through oVirt REST API");
        g_object_unref(task);
    }
}


static void ovirt_foreign_menu_refresh_cdrom_file_async(OvirtForeignMenu *menu,
                                                        GTask *task)
{
    g_return_if_fail(OVIRT_IS_RESOURCE(menu->cdrom));

    ovirt_resource_refresh_async(OVIRT_RESOURCE(menu->cdrom),
                                 menu->proxy, g_task_get_cancellable(task),
                                 cdrom_file_refreshed_cb, task);
}


static void cdroms_fetched_cb(GObject *source_object,
                              GAsyncResult *result,
                              gpointer user_data)
{
    GHashTable *cdroms;
    OvirtCollection *cdrom_collection = OVIRT_COLLECTION(source_object);
    GTask *task = G_TASK(user_data);
    OvirtForeignMenu *menu = OVIRT_FOREIGN_MENU(g_task_get_source_object(task));
    GHashTableIter iter;
    OvirtCdrom *cdrom;
    GError *error = NULL;

    ovirt_collection_fetch_finish(cdrom_collection, result, &error);
    if (error != NULL) {
        g_warning("failed to fetch cdrom collection: %s", error->message);
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    cdroms = ovirt_collection_get_resources(cdrom_collection);

    g_warn_if_fail(g_hash_table_size(cdroms) <= 1);

    g_hash_table_iter_init(&iter, cdroms);
    /* Set CDROM drive. If we have multiple ones, only the first
     * one will be kept, but currently oVirt only adds one CDROM
     * device per-VM
     */
    if (g_hash_table_iter_next(&iter, NULL, (gpointer *)&cdrom)) {
        if (menu->cdrom != NULL) {
            g_object_unref(G_OBJECT(menu->cdrom));
        }
        menu->cdrom = g_object_ref(G_OBJECT(cdrom));
        g_debug("Set VM cdrom to %p", menu->cdrom);
    }

    if (menu->cdrom != NULL) {
        ovirt_foreign_menu_next_async_step(menu, task, STATE_VM_CDROM);
    } else {
        g_debug("Could not find VM cdrom through oVirt REST API");
        g_task_return_new_error(task, OVIRT_ERROR, OVIRT_ERROR_FAILED,
                                "Could not find VM cdrom through oVirt REST API");
        g_object_unref(task);
    }
}


static void ovirt_foreign_menu_fetch_vm_cdrom_async(OvirtForeignMenu *menu,
                                                    GTask *task)
{
    OvirtCollection *cdrom_collection;

    cdrom_collection = ovirt_vm_get_cdroms(menu->vm);
    ovirt_collection_fetch_async(cdrom_collection, menu->proxy,
                                 g_task_get_cancellable(task),
                                 cdroms_fetched_cb, task);
}

static gboolean storage_domain_attached_to_data_center(OvirtStorageDomain *domain,
                                                      OvirtDataCenter *data_center)
{
    GStrv data_center_ids;
    char *data_center_guid;
    gboolean match;

    g_object_get(domain, "data-center-ids", &data_center_ids, NULL);
    g_object_get(data_center, "guid", &data_center_guid, NULL);
    match = g_strv_contains((const gchar * const *) data_center_ids, data_center_guid);
    g_strfreev(data_center_ids);
    g_free(data_center_guid);

    return match;
}

static gboolean storage_domain_validate(OvirtForeignMenu *menu G_GNUC_UNUSED,
                                        OvirtStorageDomain *domain)
{
    char *name;
    int type, state;
    gboolean ret = TRUE;

    g_object_get(domain, "name", &name, "type", &type, "state", &state, NULL);

    if (type != OVIRT_STORAGE_DOMAIN_TYPE_ISO && type != OVIRT_STORAGE_DOMAIN_TYPE_DATA) {
        g_debug("Storage domain '%s' type is not ISO or DATA", name);
        ret = FALSE;
    }

    if (state != OVIRT_STORAGE_DOMAIN_STATE_ACTIVE) {
        g_debug("Storage domain '%s' state is not active", name);
        ret = FALSE;
    }

    if (!storage_domain_attached_to_data_center(domain, menu->data_center)) {
        g_debug("Storage domain '%s' is not attached to data center", name);
        ret = FALSE;
    }

    g_debug ("Storage domain '%s' is %s", name, ret ? "valid" : "not valid");
    g_free(name);
    return ret;
}

static gboolean ovirt_foreign_menu_set_file_collection(OvirtForeignMenu *menu, OvirtCollection *file_collection)
{
    g_return_val_if_fail(file_collection != NULL, FALSE);

    if (menu->files) {
        g_object_unref(G_OBJECT(menu->files));
    }
    menu->files = g_object_ref(G_OBJECT(file_collection));
    g_debug("Set VM files to %p", menu->files);
    return TRUE;
}

static OvirtCollection *storage_domain_get_files(OvirtStorageDomain *domain)
{
    OvirtCollection *files = NULL;
    OvirtStorageDomainType type;

    if (domain == NULL)
        return NULL;

    g_object_get(domain, "type", &type, NULL);

    if (type == OVIRT_STORAGE_DOMAIN_TYPE_ISO)
        files = ovirt_storage_domain_get_files(domain);
#ifdef HAVE_OVIRT_STORAGE_DOMAIN_GET_DISKS
    else if (type == OVIRT_STORAGE_DOMAIN_TYPE_DATA)
        files = ovirt_storage_domain_get_disks(domain);
#endif

    return files;
}

static void storage_domains_fetched_cb(GObject *source_object,
                                       GAsyncResult *result,
                                       gpointer user_data)
{
    GError *error = NULL;
    GTask *task = G_TASK(user_data);
    OvirtForeignMenu *menu = OVIRT_FOREIGN_MENU(g_task_get_source_object(task));
    OvirtCollection *collection = OVIRT_COLLECTION(source_object);
    GHashTableIter iter;
    OvirtStorageDomain *domain, *valid_domain = NULL;
    OvirtCollection *file_collection;

    ovirt_collection_fetch_finish(collection, result, &error);
    if (error != NULL) {
        g_warning("failed to fetch storage domains: %s", error->message);
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    g_hash_table_iter_init(&iter, ovirt_collection_get_resources(collection));
    while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&domain)) {
        if (!storage_domain_validate(menu, domain))
            continue;

        /* Storage domain of type ISO has precedence over type DATA */
        if (valid_domain != NULL) {
            OvirtStorageDomainType domain_type, valid_type;
            g_object_get(domain, "type", &domain_type, NULL);
            g_object_get(valid_domain, "type", &valid_type, NULL);

            if (domain_type > valid_type)
                valid_domain = domain;

            continue;
        }

        valid_domain = domain;
    }

    file_collection = storage_domain_get_files(valid_domain);
    if (!ovirt_foreign_menu_set_file_collection(menu, file_collection)) {
        const char *msg = valid_domain ? "Could not find ISO file collection"
                                       : "Could not find valid ISO storage domain";

        g_debug("%s", msg);
        g_task_return_new_error(task, OVIRT_ERROR, OVIRT_ERROR_FAILED, "%s", msg);
        g_object_unref(task);
        return;
    }

    ovirt_foreign_menu_next_async_step(menu, task, STATE_STORAGE_DOMAIN);
}


static void ovirt_foreign_menu_fetch_storage_domain_async(OvirtForeignMenu *menu,
                                                          GTask *task)
{
    OvirtCollection *collection = NULL;

    g_return_if_fail(OVIRT_IS_FOREIGN_MENU(menu));
    g_return_if_fail(OVIRT_IS_PROXY(menu->proxy));
    g_return_if_fail(OVIRT_IS_DATA_CENTER(menu->data_center));

    collection = ovirt_data_center_get_storage_domains(menu->data_center);

    g_debug("Start fetching iso file collection");
    ovirt_collection_fetch_async(collection, menu->proxy,
                                 g_task_get_cancellable(task),
                                 storage_domains_fetched_cb, task);
}


static void data_center_fetched_cb(GObject *source_object,
                                   GAsyncResult *result,
                                   gpointer user_data)
{
    GError *error = NULL;
    GTask *task = G_TASK(user_data);
    OvirtForeignMenu *menu = OVIRT_FOREIGN_MENU(g_task_get_source_object(task));
    OvirtResource *resource = OVIRT_RESOURCE(source_object);

    ovirt_resource_refresh_finish(resource, result, &error);
    if (error != NULL) {
        g_debug("failed to fetch Data Center: %s", error->message);
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    ovirt_foreign_menu_next_async_step(menu, task, STATE_DATA_CENTER);
}


static void ovirt_foreign_menu_fetch_data_center_async(OvirtForeignMenu *menu,
                                                       GTask *task)
{
    g_return_if_fail(OVIRT_IS_FOREIGN_MENU(menu));
    g_return_if_fail(OVIRT_IS_PROXY(menu->proxy));
    g_return_if_fail(OVIRT_IS_CLUSTER(menu->cluster));

    menu->data_center = ovirt_cluster_get_data_center(menu->cluster);
    ovirt_resource_refresh_async(OVIRT_RESOURCE(menu->data_center),
                                 menu->proxy,
                                 g_task_get_cancellable(task),
                                 data_center_fetched_cb,
                                 task);
}


static void cluster_fetched_cb(GObject *source_object,
                               GAsyncResult *result,
                               gpointer user_data)
{
    GError *error = NULL;
    GTask *task = G_TASK(user_data);
    OvirtForeignMenu *menu = OVIRT_FOREIGN_MENU(g_task_get_source_object(task));
    OvirtResource *resource = OVIRT_RESOURCE(source_object);

    ovirt_resource_refresh_finish(resource, result, &error);
    if (error != NULL) {
        g_debug("failed to fetch Cluster: %s", error->message);
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    ovirt_foreign_menu_next_async_step(menu, task, STATE_CLUSTER);
}


static void ovirt_foreign_menu_fetch_cluster_async(OvirtForeignMenu *menu,
                                                   GTask *task)
{
    g_return_if_fail(OVIRT_IS_FOREIGN_MENU(menu));
    g_return_if_fail(OVIRT_IS_PROXY(menu->proxy));
    g_return_if_fail(OVIRT_IS_HOST(menu->host));

    menu->cluster = ovirt_host_get_cluster(menu->host);
    ovirt_resource_refresh_async(OVIRT_RESOURCE(menu->cluster),
                                 menu->proxy,
                                 g_task_get_cancellable(task),
                                 cluster_fetched_cb,
                                 task);
}


static void host_fetched_cb(GObject *source_object,
                            GAsyncResult *result,
                            gpointer user_data)
{
    GError *error = NULL;
    GTask *task = G_TASK(user_data);
    OvirtForeignMenu *menu = OVIRT_FOREIGN_MENU(g_task_get_source_object(task));
    OvirtResource *resource = OVIRT_RESOURCE(source_object);

    ovirt_resource_refresh_finish(resource, result, &error);
    if (error != NULL) {
        g_debug("failed to fetch Host: %s", error->message);
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    ovirt_foreign_menu_next_async_step(menu, task, STATE_HOST);
}


static void ovirt_foreign_menu_fetch_host_async(OvirtForeignMenu *menu,
                                                GTask *task)
{
    g_return_if_fail(OVIRT_IS_FOREIGN_MENU(menu));
    g_return_if_fail(OVIRT_IS_PROXY(menu->proxy));
    g_return_if_fail(OVIRT_IS_VM(menu->vm));

    menu->host = ovirt_vm_get_host(menu->vm);
    ovirt_resource_refresh_async(OVIRT_RESOURCE(menu->host),
                                 menu->proxy,
                                 g_task_get_cancellable(task),
                                 host_fetched_cb,
                                 task);
}


static void vms_fetched_cb(GObject *source_object,
                           GAsyncResult *result,
                           gpointer user_data)
{
    GError *error = NULL;
    GTask *task = G_TASK(user_data);
    OvirtForeignMenu *menu = OVIRT_FOREIGN_MENU(g_task_get_source_object(task));
    OvirtCollection *collection = OVIRT_COLLECTION(source_object);
    GHashTableIter iter;
    OvirtVm *vm;

    ovirt_collection_fetch_finish(collection, result, &error);
    if (error != NULL) {
        g_debug("failed to fetch VM list: %s", error->message);
        g_task_return_error(task, error);
        g_object_unref(task);
        goto end;
    }

    g_hash_table_iter_init(&iter, ovirt_collection_get_resources(collection));
    while (g_hash_table_iter_next(&iter, NULL, (gpointer *)&vm)) {
        char *guid;

        g_object_get(G_OBJECT(vm), "guid", &guid, NULL);
        if (g_strcmp0(guid, menu->vm_guid) == 0) {
            menu->vm = g_object_ref(vm);
            g_free(guid);
            break;
        }
        g_free(guid);
    }
    if (menu->vm != NULL) {
        ovirt_foreign_menu_next_async_step(menu, task, STATE_VM);
    } else {
        g_warning("failed to find a VM with guid \"%s\"", menu->vm_guid);
        g_task_return_new_error(task, OVIRT_ERROR, OVIRT_ERROR_FAILED,
                                "Could not find a VM with guid \"%s\"", menu->vm_guid);
        g_object_unref(task);
    }

end:
    g_object_unref(collection);
}


static void ovirt_foreign_menu_fetch_vm_async(OvirtForeignMenu *menu,
                                              GTask *task)
{
    OvirtCollection *vms;

    g_return_if_fail(OVIRT_IS_FOREIGN_MENU(menu));
    g_return_if_fail(OVIRT_IS_PROXY(menu->proxy));
    g_return_if_fail(OVIRT_IS_API(menu->api));

    char * query = g_strdup_printf("id=%s", menu->vm_guid);
    vms = ovirt_api_search_vms(menu->api, query);
    g_free(query);

    ovirt_collection_fetch_async(vms, menu->proxy,
                                 g_task_get_cancellable(task),
                                 vms_fetched_cb, task);
}


static void api_fetched_cb(GObject *source_object,
                           GAsyncResult *result,
                           gpointer user_data)
{
    GError *error = NULL;
    GTask *task = G_TASK(user_data);
    OvirtForeignMenu *menu = OVIRT_FOREIGN_MENU(g_task_get_source_object(task));
    OvirtProxy *proxy = OVIRT_PROXY(source_object);

    menu->api = ovirt_proxy_fetch_api_finish(proxy, result, &error);
    if (error != NULL) {
        g_debug("failed to fetch toplevel API object: %s", error->message);
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }
    g_return_if_fail(OVIRT_IS_API(menu->api));
    g_object_ref(menu->api);

    ovirt_foreign_menu_next_async_step(menu, task, STATE_API);
}


static void ovirt_foreign_menu_fetch_api_async(OvirtForeignMenu *menu,
                                               GTask *task)
{
    g_debug("Start fetching oVirt main entry point");

    g_return_if_fail(OVIRT_IS_FOREIGN_MENU(menu));
    g_return_if_fail(OVIRT_IS_PROXY(menu->proxy));

    ovirt_proxy_fetch_api_async(menu->proxy,
                                g_task_get_cancellable(task),
                                api_fetched_cb, task);
}


static void iso_list_fetched_cb(GObject *source_object,
                                GAsyncResult *result,
                                gpointer user_data)
{
    GTask *task = G_TASK(user_data);
    OvirtForeignMenu *menu = OVIRT_FOREIGN_MENU(g_task_get_source_object(task));
    OvirtCollection *collection = OVIRT_COLLECTION(source_object);
    GError *error = NULL;
    GList *files;

    ovirt_collection_fetch_finish(collection, result, &error);
    if (error != NULL) {
        g_warning("failed to fetch files for ISO storage domain: %s",
                   error->message);
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    files = g_hash_table_get_values(ovirt_collection_get_resources(collection));
    ovirt_foreign_menu_set_files(menu, files);
    g_list_free(files);
    g_task_return_pointer(task, menu->iso_names, NULL);
    g_object_unref(task);
}


static void ovirt_foreign_menu_fetch_iso_list_async(OvirtForeignMenu *menu,
                                                    GTask *task)
{
    if (menu->files == NULL) {
        return;
    }

    ovirt_collection_fetch_async(menu->files, menu->proxy,
                                 g_task_get_cancellable(task),
                                 iso_list_fetched_cb, task);
}


OvirtForeignMenu *ovirt_foreign_menu_new_from_file(VirtViewerFile *file)
{
    OvirtProxy *proxy = NULL;
    OvirtForeignMenu *menu = NULL;
    gboolean admin;
    char *ca_str = NULL;
    char *jsessionid = NULL;
    char *sso_token = NULL;
    char *url = NULL;
    char *vm_guid = NULL;
    GByteArray *ca = NULL;

    url = virt_viewer_file_get_ovirt_host(file);
    vm_guid = virt_viewer_file_get_ovirt_vm_guid(file);
    jsessionid = virt_viewer_file_get_ovirt_jsessionid(file);
    sso_token = virt_viewer_file_get_ovirt_sso_token(file);
    ca_str = virt_viewer_file_get_ovirt_ca(file);
    admin = virt_viewer_file_get_ovirt_admin(file);

    if ((url == NULL) || (vm_guid == NULL)) {
        g_debug("ignoring [ovirt] section content as URL, VM GUID"
                " are missing from the .vv file");
        goto end;
    }

    if ((jsessionid == NULL) && (sso_token == NULL)) {
        g_debug("ignoring [ovirt] section content as jsessionid and sso-token"
                " are both missing from the .vv file");
        goto end;
    }

    proxy = ovirt_proxy_new(url);
    if (proxy == NULL)
        goto end;

    if (ca_str != NULL) {
        ca = g_byte_array_new_take((guint8 *)ca_str, strlen(ca_str) + 1);
        ca_str = NULL;
    }

    g_object_set(G_OBJECT(proxy),
                 "admin", admin,
                 "ca-cert", ca,
                 NULL);
    if (jsessionid != NULL) {
        g_object_set(G_OBJECT(proxy),
                     "session-id", jsessionid,
                     NULL);
    }
    if (sso_token != NULL) {
        g_object_set(G_OBJECT(proxy),
                     "sso-token", sso_token,
                     NULL);
    }

    menu = g_object_new(OVIRT_TYPE_FOREIGN_MENU,
                        "proxy", proxy,
                        "vm-guid", vm_guid,
                        NULL);

end:
    g_free(url);
    g_free(vm_guid);
    g_free(jsessionid);
    g_free(sso_token);
    g_free(ca_str);
    if (ca != NULL) {
        g_byte_array_unref(ca);
    }

    return menu;
}
