#include "tray.h"

#ifdef _WIN32
static NOTIFYICONDATA nid;
static HWND main_hwnd = NULL;
static HMENU tray_menu = NULL;
static HWND tray_hwnd = NULL;
static WNDPROC original_wndproc = NULL;

// Custom menu commands
#define ID_TRAY_RESTORE 1001
#define ID_TRAY_EXIT    1002

// Callback function for delete-event signal
static gboolean on_delete_event(GtkWidget *w, GdkEvent *e, gpointer d) {
    ShowWindow(main_hwnd, SW_HIDE);
    return TRUE;
}

// Hidden window procedure for tray messages
static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_APP + 1) {
        if (lParam == WM_LBUTTONDBLCLK) {
            // Restore on double-click
            ShowWindow(main_hwnd, SW_SHOW);
            SetForegroundWindow(main_hwnd);
        } else if (lParam == WM_RBUTTONUP) {
            // Show context menu
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(tray_menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
        }
    } else if (msg == WM_COMMAND) {
        switch (LOWORD(wParam)) {
        case ID_TRAY_RESTORE:
            ShowWindow(main_hwnd, SW_SHOW);
            SetForegroundWindow(main_hwnd);
            break;
        case ID_TRAY_EXIT:
            gtk_main_quit();
            break;
        }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void tray_init(GtkWidget *window, const char *tooltip) {
    // Make sure the window is realized first
    if (!gtk_widget_get_realized(window)) {
        gtk_widget_realize(window);
    }
    
    GdkWindow *gdk_window = gtk_widget_get_window(window);
    if (!gdk_window) {
        g_error("Failed to get GdkWindow - window not realized");
        return;
    }
    
    main_hwnd = GDK_WINDOW_HWND(gdk_window);
    if (!main_hwnd) {
        g_error("Failed to get HWND from GdkWindow");
        return;
    }
    
    // Create a hidden window to receive tray messages
    WNDCLASS wc = {0};
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TEXT("TrayWindowClass");
    RegisterClass(&wc);
    
    tray_hwnd = CreateWindow(
        TEXT("TrayWindowClass"),
        TEXT("Tray Window"),
        0, 0, 0, 0, 0,
        HWND_MESSAGE,  // Message-only window
        NULL,
        GetModuleHandle(NULL),
        NULL
    );
    
    // Create tray icon
    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = tray_hwnd;  // Use hidden window for messages
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP + 1;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpyn(nid.szTip, tooltip, sizeof(nid.szTip) / sizeof(TCHAR));
    Shell_NotifyIcon(NIM_ADD, &nid);
    
    // Build right-click menu
    tray_menu = CreatePopupMenu();
    AppendMenu(tray_menu, MF_STRING, ID_TRAY_RESTORE, TEXT("Restore"));
    AppendMenu(tray_menu, MF_STRING, ID_TRAY_EXIT, TEXT("Exit"));
    
    // Intercept close button → hide instead of quit
    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), NULL);
}

void tray_cleanup(void) {
    Shell_NotifyIcon(NIM_DELETE, &nid);
    if (tray_menu) {
        DestroyMenu(tray_menu);
        tray_menu = NULL;
    }
    if (tray_hwnd) {
        DestroyWindow(tray_hwnd);
        tray_hwnd = NULL;
    }
}

#endif