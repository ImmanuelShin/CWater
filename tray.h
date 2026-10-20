#pragma once

#include <gtk/gtk.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#include <gdk/gdkwin32.h>

void tray_init(GtkWidget *window, const char *tooltip);
void tray_cleanup(void);
#endif
