/*
 * Virt Viewer: A virtual machine console viewer
 *
 * Copyright (C) 2007-2012 Red Hat, Inc.
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
 */
#ifndef _VIRT_VIEWER_DISPLAY_SPICE_H
#define _VIRT_VIEWER_DISPLAY_SPICE_H

#include <glib-object.h>
#include <spice-client.h>

#include "virt-viewer-display.h"
#include "virt-viewer-session-spice.h"

G_BEGIN_DECLS

#define VIRT_VIEWER_TYPE_DISPLAY_SPICE virt_viewer_display_spice_get_type()
G_DECLARE_FINAL_TYPE(VirtViewerDisplaySpice,
                     virt_viewer_display_spice,
                     VIRT_VIEWER,
                     DISPLAY_SPICE,
                     VirtViewerDisplay)

GType virt_viewer_display_spice_get_type(void);

GtkWidget* virt_viewer_display_spice_new(VirtViewerSessionSpice *session, SpiceChannel *channel, gint monitorid);

void virt_viewer_display_spice_set_desktop(VirtViewerDisplay *display, guint x, guint y,
                                           guint width, guint height);
G_END_DECLS

#endif /* _VIRT_VIEWER_DISPLAY_SPICE_H */
