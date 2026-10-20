#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <gtk/gtk.h>
#include <windows.h>
#include "tray.h"

// --- Data Structures  ---

typedef struct {
    int id;
    int interval_minutes;
    time_t start_time;
    time_t next_reminder_time;
    int is_active;
} Reminder;

typedef struct {
    time_t log_time;
    int amount_oz;
} DrinkLogEntry;

typedef struct {
    time_t date;
    int count;
    int total_oz;
    DrinkLogEntry entries[100];
    int num_entries;
} DailyLog;

// --- Global Variables ---
Reminder* reminders = NULL;
GPtrArray *reminder_labels = NULL;
int num_reminders = 0;
int max_reminders_capacity = 5;
int next_reminder_id = 1;

DailyLog* day_logs = NULL;
int num_day_logs = 0;
int max_day_logs_capacity = 8;
int current_day_index = -1;
int daily_water_goal_oz = 64;
time_t last_checked_date;

// GTK Widgets
GtkWidget *main_window;
GtkWidget *water_label;
GtkWidget *reminders_list;
GtkWidget *progress_bar;
GtkWidget *status_label;

// --- Constants ---
#define REMINDERS_FILE "reminders.txt"
#define DRINK_LOGS_FILE "drink_logs.json"
#define LEGACY_DRINK_LOGS_FILE "drink_logs.txt"
#define MAX_LINE_LENGTH 256
#define DEFAULT_WATER_GOAL_OZ 64
#define MAX_ENTRIES_PER_DAY 100

// --- Function Prototypes ---
// Core functions
void get_current_date_midnight(time_t* date_out);
void get_midnight_for_time(time_t t, time_t* date_out);
void format_time(time_t t, char* buffer, size_t buffer_size);
void format_time_with_date(time_t t, char* buffer, size_t buffer_size);
void format_date_ymd(time_t t, char* buffer, size_t buffer_size);
int parse_ymd_to_midnight(const char* ymd, time_t* out);
void init_reminders_array();
void resize_reminders_array();
void add_reminder_to_list(int interval, time_t start_time);
void calculate_next_reminder_for_single(Reminder* r);
void check_reminders();
void delete_reminder_by_id(int id);
void init_day_logs_array();
void resize_day_logs_array();
DailyLog* get_current_day();
int find_or_create_day_index(time_t midnight);
void ensure_today_slot();
void add_drink_entry(DailyLog* day, time_t log_time, int amount_oz);
void log_drink(int amount_oz);
void check_date_change();
void save_reminders();
void load_reminders();
void save_drink_logs();
void import_legacy_drink_logs();
void load_drink_logs();
void init_app();
void cleanup_app();

// GUI functions
void apply_progress_bar_style();
void update_water_display();
void update_reminders_list();
void update_progress_bar();
void show_notification(const char* message);
gboolean timer_callback(gpointer data);

// GTK Callbacks
void on_log_8oz_clicked(GtkWidget *widget, gpointer data);
void on_log_16oz_clicked(GtkWidget *widget, gpointer data);
void on_log_custom_clicked(GtkWidget *widget, gpointer data);
void on_set_goal_clicked(GtkWidget *widget, gpointer data);
void on_add_reminder_clicked(GtkWidget *widget, gpointer data);
void on_delete_reminder_clicked(GtkWidget *widget, gpointer data);
void on_window_destroy(GtkWidget *widget, gpointer data);

// --- Utility Functions ---

void get_current_date_midnight(time_t* date_out) {
    get_midnight_for_time(time(NULL), date_out);
}

void get_midnight_for_time(time_t t, time_t* date_out) {
    struct tm tm_info = *localtime(&t);
    tm_info.tm_hour = 0;
    tm_info.tm_min = 0;
    tm_info.tm_sec = 0;
    tm_info.tm_isdst = -1;
    *date_out = mktime(&tm_info);
}

void format_time(time_t t, char* buffer, size_t buffer_size) {
    struct tm tm_info = *localtime(&t);
    strftime(buffer, buffer_size, "%H:%M:%S", &tm_info);
}

void format_time_with_date(time_t t, char* buffer, size_t buffer_size) {
    struct tm tm_info = *localtime(&t);
    strftime(buffer, buffer_size, "%m/%d %H:%M:%S", &tm_info);
}

void format_date_ymd(time_t t, char* buffer, size_t buffer_size) {
    struct tm tm_info = *localtime(&t);
    strftime(buffer, buffer_size, "%Y-%m-%d", &tm_info);
}

int parse_ymd_to_midnight(const char* ymd, time_t* out) {
    int year, month, day;
    if (sscanf(ymd, "%d-%d-%d", &year, &month, &day) != 3) {
        return 0;
    }
    struct tm tm_info = {0};
    tm_info.tm_year = year - 1900;
    tm_info.tm_mon = month - 1;
    tm_info.tm_mday = day;
    tm_info.tm_isdst = -1;
    *out = mktime(&tm_info);
    return *out != (time_t)-1;
}

static int file_exists(const char* path) {
    FILE* fp = fopen(path, "r");
    if (fp == NULL) return 0;
    fclose(fp);
    return 1;
}

static char* read_entire_file(const char* path) {
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    char* buf = (char*)malloc((size_t)len + 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    size_t nread = fread(buf, 1, (size_t)len, fp);
    buf[nread] = '\0';
    fclose(fp);
    return buf;
}

// --- Core Logic Functions ---

void init_reminders_array() {
    reminders = (Reminder*)malloc(max_reminders_capacity * sizeof(Reminder));
    if (reminders == NULL) {
        fprintf(stderr, "Failed to allocate memory for reminders\n");
        exit(EXIT_FAILURE);
    }
}

void resize_reminders_array() {
    max_reminders_capacity *= 2;
    reminders = (Reminder*)realloc(reminders, max_reminders_capacity * sizeof(Reminder));
    if (reminders == NULL) {
        fprintf(stderr, "Failed to reallocate memory for reminders\n");
        exit(EXIT_FAILURE);
    }
}

void add_reminder_to_list(int interval, time_t start_time) {
    if (num_reminders >= max_reminders_capacity) {
        resize_reminders_array();
    }

    Reminder new_reminder;
    new_reminder.id = next_reminder_id++;
    new_reminder.interval_minutes = interval;
    new_reminder.start_time = start_time;
    new_reminder.is_active = 1;

    time_t now = time(NULL);
    if (start_time == 0) {
        new_reminder.start_time = now;
        new_reminder.next_reminder_time = now + (long)interval * 60;
    } else {
        struct tm current_tm = *localtime(&now);
        struct tm start_tm = *localtime(&start_time);

        current_tm.tm_hour = start_tm.tm_hour;
        current_tm.tm_min = start_tm.tm_min;
        current_tm.tm_sec = start_tm.tm_sec;

        time_t target_time = mktime(&current_tm);

        if (target_time < now) {
            long minutes_since = (long)(now - target_time) / 60;
            int intervals_passed = (int)(minutes_since / interval) + 1;
            new_reminder.next_reminder_time = target_time + (long)interval * 60 * intervals_passed;
        } else {
            new_reminder.next_reminder_time = target_time;
        }
    }

    reminders[num_reminders++] = new_reminder;
    save_reminders();
    update_reminders_list();
}

void calculate_next_reminder_for_single(Reminder* r) {
    time_t now = time(NULL);
    long time_passed_seconds = now - r->start_time;
    long interval_seconds = (long)r->interval_minutes * 60;

    if (interval_seconds <= 0) {
        r->next_reminder_time = now + 60;
        return;
    }

    long intervals_passed = time_passed_seconds / interval_seconds;
    r->next_reminder_time = r->start_time + (intervals_passed + 1) * interval_seconds;
}

void check_reminders() {
    time_t now = time(NULL);
    for (int i = 0; i < num_reminders; i++) {
        if (reminders[i].is_active && now >= reminders[i].next_reminder_time) {
            show_notification("💧 Time to drink some water! Stay hydrated!");
            calculate_next_reminder_for_single(&reminders[i]);
            save_reminders();
            update_reminders_list();
        }
    }
}

void delete_reminder_by_id(int id) {
    for (int i = 0; i < num_reminders; i++) {
        if (reminders[i].id == id) {
            for (int j = i; j < num_reminders - 1; j++) {
                reminders[j] = reminders[j + 1];
            }
            num_reminders--;
            save_reminders();
            update_reminders_list();
            break;
        }
    }
}

void init_day_logs_array() {
    day_logs = (DailyLog*)malloc(max_day_logs_capacity * sizeof(DailyLog));
    if (day_logs == NULL) {
        fprintf(stderr, "Failed to allocate memory for drink logs\n");
        exit(EXIT_FAILURE);
    }
    num_day_logs = 0;
    current_day_index = -1;
}

void resize_day_logs_array() {
    max_day_logs_capacity *= 2;
    day_logs = (DailyLog*)realloc(day_logs, max_day_logs_capacity * sizeof(DailyLog));
    if (day_logs == NULL) {
        fprintf(stderr, "Failed to reallocate memory for drink logs\n");
        exit(EXIT_FAILURE);
    }
}

DailyLog* get_current_day() {
    if (current_day_index < 0 || current_day_index >= num_day_logs) {
        return NULL;
    }
    return &day_logs[current_day_index];
}

int find_or_create_day_index(time_t midnight) {
    for (int i = 0; i < num_day_logs; i++) {
        if (day_logs[i].date == midnight) {
            return i;
        }
    }
    if (num_day_logs >= max_day_logs_capacity) {
        resize_day_logs_array();
    }
    int idx = num_day_logs++;
    memset(&day_logs[idx], 0, sizeof(DailyLog));
    day_logs[idx].date = midnight;
    return idx;
}

void ensure_today_slot() {
    time_t today_midnight;
    get_current_date_midnight(&today_midnight);
    current_day_index = find_or_create_day_index(today_midnight);
}

void add_drink_entry(DailyLog* day, time_t log_time, int amount_oz) {
    if (day == NULL || amount_oz <= 0) return;
    if (day->num_entries >= MAX_ENTRIES_PER_DAY) return;

    day->entries[day->num_entries].log_time = log_time;
    day->entries[day->num_entries].amount_oz = amount_oz;
    day->num_entries++;
    day->count++;
    day->total_oz += amount_oz;
}

void log_drink(int amount_oz) {
    if (amount_oz <= 0) return;

    ensure_today_slot();
    add_drink_entry(get_current_day(), time(NULL), amount_oz);

    save_drink_logs();
    update_water_display();
    update_progress_bar();
}

void check_date_change() {
    time_t now_midnight;
    get_current_date_midnight(&now_midnight);

    if (now_midnight != last_checked_date) {
        ensure_today_slot();
        last_checked_date = now_midnight;
        save_drink_logs();
        update_water_display();
        update_progress_bar();
    }
}

// --- File I/O ---

void save_reminders() {
    FILE* fp = fopen(REMINDERS_FILE, "w");
    if (fp == NULL) return;

    for (int i = 0; i < num_reminders; i++) {
        if (reminders[i].is_active) {
            fprintf(fp, "%d,%d,%ld,%ld\n",
                    reminders[i].id,
                    reminders[i].interval_minutes,
                    (long)reminders[i].start_time,
                    (long)reminders[i].next_reminder_time);
        }
    }
    fclose(fp);
}

void load_reminders() {
    FILE* fp = fopen(REMINDERS_FILE, "r");
    if (fp == NULL) return;

    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), fp) != NULL) {
        int id, interval;
        long start_time_long, next_reminder_time_long;

        if (sscanf(line, "%d,%d,%ld,%ld", &id, &interval, &start_time_long, &next_reminder_time_long) == 4) {
            if (num_reminders >= max_reminders_capacity) {
                resize_reminders_array();
            }
            reminders[num_reminders].id = id;
            reminders[num_reminders].interval_minutes = interval;
            reminders[num_reminders].start_time = (time_t)start_time_long;
            reminders[num_reminders].next_reminder_time = (time_t)next_reminder_time_long;
            reminders[num_reminders].is_active = 1;
            num_reminders++;
            if (id >= next_reminder_id) {
                next_reminder_id = id + 1;
            }
        }
    }
    fclose(fp);
}

void save_drink_logs() {
    FILE* fp = fopen(DRINK_LOGS_FILE, "w");
    if (fp == NULL) return;

    fprintf(fp, "{\n  \"goal_oz\": %d,\n  \"days\": [\n", daily_water_goal_oz);

    for (int i = 0; i < num_day_logs; i++) {
        char date_buf[16];
        format_date_ymd(day_logs[i].date, date_buf, sizeof(date_buf));
        fprintf(fp, "    {\n      \"date\": \"%s\",\n      \"entries\": [\n", date_buf);

        for (int j = 0; j < day_logs[i].num_entries; j++) {
            fprintf(fp, "        { \"ts\": %ld, \"oz\": %d }%s\n",
                    (long)day_logs[i].entries[j].log_time,
                    day_logs[i].entries[j].amount_oz,
                    (j < day_logs[i].num_entries - 1) ? "," : "");
        }

        fprintf(fp, "      ]\n    }%s\n", (i < num_day_logs - 1) ? "," : "");
    }

    fprintf(fp, "  ]\n}\n");
    fclose(fp);
}

void import_legacy_drink_logs() {
    FILE* fp = fopen(LEGACY_DRINK_LOGS_FILE, "r");
    if (fp == NULL) return;

    char line[MAX_LINE_LENGTH];
    while (fgets(line, sizeof(line), fp) != NULL) {
        long log_time_long;
        int amount_oz;
        if (sscanf(line, "%ld,%d", &log_time_long, &amount_oz) != 2) {
            continue;
        }
        if (amount_oz <= 0) continue;

        time_t entry_time = (time_t)log_time_long;
        time_t midnight;
        get_midnight_for_time(entry_time, &midnight);
        int idx = find_or_create_day_index(midnight);
        add_drink_entry(&day_logs[idx], entry_time, amount_oz);
    }
    fclose(fp);
}

void load_drink_logs() {
    daily_water_goal_oz = DEFAULT_WATER_GOAL_OZ;

    if (file_exists(DRINK_LOGS_FILE)) {
        char* buf = read_entire_file(DRINK_LOGS_FILE);
        if (buf != NULL) {
            const char* goal_key = strstr(buf, "\"goal_oz\"");
            if (goal_key != NULL) {
                const char* colon = strchr(goal_key, ':');
                if (colon != NULL) {
                    int goal = atoi(colon + 1);
                    if (goal > 0) {
                        daily_water_goal_oz = goal;
                    }
                }
            }

            const char* p = buf;
            while ((p = strstr(p, "\"date\"")) != NULL) {
                char ymd[16];
                if (sscanf(p, "\"date\": \"%15[0-9-]\"", ymd) != 1) {
                    p += 6;
                    continue;
                }

                time_t midnight;
                if (!parse_ymd_to_midnight(ymd, &midnight)) {
                    p += 6;
                    continue;
                }

                int idx = find_or_create_day_index(midnight);
                const char* entries = strstr(p, "\"entries\"");
                const char* next_date = strstr(p + 6, "\"date\"");
                const char* entries_end = (next_date != NULL) ? next_date : (buf + strlen(buf));

                if (entries != NULL && entries < entries_end) {
                    const char* q = entries;
                    while (q < entries_end) {
                        const char* ts_key = strstr(q, "\"ts\"");
                        if (ts_key == NULL || ts_key >= entries_end) break;

                        long ts;
                        int oz;
                        if (sscanf(ts_key, "\"ts\": %ld, \"oz\": %d", &ts, &oz) == 2) {
                            add_drink_entry(&day_logs[idx], (time_t)ts, oz);
                        }
                        q = ts_key + 4;
                    }
                }

                p += 6;
            }
            free(buf);
        }
    } else if (file_exists(LEGACY_DRINK_LOGS_FILE)) {
        import_legacy_drink_logs();
        ensure_today_slot();
        save_drink_logs();
        return;
    }

    ensure_today_slot();
}

// --- GUI Functions ---

void apply_progress_bar_style() {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "progressbar, progressbar.horizontal {"
        "  min-height: 14px;"
        "}"
        "progressbar trough, progressbar.horizontal trough {"
        "  min-height: 14px;"
        "}"
        "progressbar progress, progressbar.horizontal trough progress {"
        "  min-height: 14px;"
        "  background-image: none;"
        "  background-color: #4a90d9;"
        "  border: none;"
        "}",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void update_water_display() {
    DailyLog* day = get_current_day();
    int count = day ? day->count : 0;
    int total_oz = day ? day->total_oz : 0;

    char buffer[256];
    snprintf(buffer, sizeof(buffer), "💧 Today: %d drinks, %d oz total",
             count, total_oz);
    gtk_label_set_text(GTK_LABEL(water_label), buffer);
}

void update_reminders_list() {
    while (reminder_labels->len < num_reminders) {
        GtkWidget *label = gtk_label_new("");
        gtk_label_set_xalign(GTK_LABEL(label), 0.0);
        gtk_box_pack_start(GTK_BOX(reminders_list), label, FALSE, FALSE, 2);
        g_ptr_array_add(reminder_labels, label);
    }

    while (reminder_labels->len > num_reminders) {
        GtkWidget *label = g_ptr_array_index(reminder_labels, reminder_labels->len - 1);
        g_ptr_array_remove_index(reminder_labels, reminder_labels->len - 1);
    }

    time_t now = time(NULL);
    for (int i = 0; i < num_reminders; i++) {
        if (reminders[i].is_active) {
            char buffer[512];
            char start_time_buffer[64];
            char next_time_buffer[64];

            long seconds_until = reminders[i].next_reminder_time - now;
            format_time(reminders[i].start_time, start_time_buffer, sizeof(start_time_buffer));

            if (seconds_until > 0) {
                int minutes = seconds_until / 60;
                int secs = seconds_until % 60;
                format_time(reminders[i].next_reminder_time, next_time_buffer, sizeof(next_time_buffer));
                snprintf(buffer, sizeof(buffer),
                         "⏰ ID %d: Every %d min from %s | Next: %s (%d:%02d remaining)",
                         reminders[i].id, reminders[i].interval_minutes,
                         start_time_buffer, next_time_buffer, minutes, secs);
            } else {
                snprintf(buffer, sizeof(buffer),
                         "⏰ ID %d: Every %d min from %s | Due now!",
                         reminders[i].id, reminders[i].interval_minutes,
                         start_time_buffer);
            }

            GtkWidget *label = g_ptr_array_index(reminder_labels, i);
            gtk_label_set_text(GTK_LABEL(label), buffer);
        }
    }

    gtk_widget_show_all(reminders_list);
}

void update_progress_bar() {
    DailyLog* day = get_current_day();
    int total_oz = day ? day->total_oz : 0;
    int goal = daily_water_goal_oz > 0 ? daily_water_goal_oz : DEFAULT_WATER_GOAL_OZ;

    double fraction = (double)total_oz / (double)goal;
    if (fraction > 1.0) fraction = 1.0;
    if (fraction < 0.0) fraction = 0.0;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(progress_bar), fraction);

    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%d/%d oz (%.0f%%)",
             total_oz, goal, fraction * 100.0);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(progress_bar), buffer);
}

void show_notification(const char* message) {
    gtk_label_set_text(GTK_LABEL(status_label), message);
#ifdef _WIN32
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;  // Hide the window
    ZeroMemory(&pi, sizeof(pi));

    char command[1024];
    snprintf(command, sizeof(command),
        "powershell.exe -WindowStyle Hidden -ExecutionPolicy Bypass -File toast.ps1 \"Water Reminder\" \"%s\"",
        message);

    if (CreateProcess(NULL, command, NULL, NULL, FALSE,
                      CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        // Don't wait for the process to complete
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
#endif
}

gboolean timer_callback(gpointer data) {
    (void)data;
    check_date_change();
    check_reminders();
    update_reminders_list();
    return TRUE;
}

// --- GTK Callbacks ---

void on_log_8oz_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    log_drink(8);
    gtk_label_set_text(GTK_LABEL(status_label), "Logged 8 oz of water.");
}

void on_log_16oz_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    log_drink(16);
    gtk_label_set_text(GTK_LABEL(status_label), "Logged 16 oz of water.");
}

void on_log_custom_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Log Custom Amount",
                                                   GTK_WINDOW(main_window),
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   "OK", GTK_RESPONSE_OK,
                                                   "Cancel", GTK_RESPONSE_CANCEL,
                                                   NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new("Enter amount in ounces:");
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), "12");

    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 5);
    gtk_widget_show_all(dialog);

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
        int amount = atoi(text);
        if (amount > 0) {
            log_drink(amount);
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Logged %d oz of water.", amount);
            gtk_label_set_text(GTK_LABEL(status_label), buffer);
        }
    }
    gtk_widget_destroy(dialog);
}

void on_set_goal_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Set Daily Goal",
                                                   GTK_WINDOW(main_window),
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   "OK", GTK_RESPONSE_OK,
                                                   "Cancel", GTK_RESPONSE_CANCEL,
                                                   NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new("Enter daily goal in ounces:");
    GtkWidget *entry = gtk_entry_new();

    char goal_text[16];
    snprintf(goal_text, sizeof(goal_text), "%d", daily_water_goal_oz);
    gtk_entry_set_text(GTK_ENTRY(entry), goal_text);

    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 5);
    gtk_widget_show_all(dialog);

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
        int goal = atoi(text);
        if (goal > 0) {
            daily_water_goal_oz = goal;
            save_drink_logs();
            update_progress_bar();
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "Daily goal set to %d oz.", goal);
            gtk_label_set_text(GTK_LABEL(status_label), buffer);
        } else {
            gtk_label_set_text(GTK_LABEL(status_label), "Goal must be greater than 0.");
        }
    }
    gtk_widget_destroy(dialog);
}

void on_add_reminder_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Add Reminder",
                                                   GTK_WINDOW(main_window),
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   "OK", GTK_RESPONSE_OK,
                                                   "Cancel", GTK_RESPONSE_CANCEL,
                                                   NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));

    // Interval input
    GtkWidget *interval_label = gtk_label_new("Enter interval in minutes:");
    GtkWidget *interval_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(interval_entry), "60");

    // Start time option
    GtkWidget *start_time_frame = gtk_frame_new("Start Time");
    GtkWidget *start_time_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(start_time_vbox), 5);

    // Radio buttons for start time options
    GtkWidget *start_now_radio = gtk_radio_button_new_with_label(NULL, "Start now");
    GtkWidget *start_specific_radio = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(start_now_radio), "Start at specific time:");

    // Time input fields
    GtkWidget *time_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *hour_label = gtk_label_new("Hour (0-23):");
    GtkWidget *hour_entry = gtk_entry_new();
    GtkWidget *minute_label = gtk_label_new("Minute (0-59):");
    GtkWidget *minute_entry = gtk_entry_new();

    // Set default time to current time
    time_t now = time(NULL);
    struct tm current_time = *localtime(&now);
    char hour_str[8], minute_str[8];
    snprintf(hour_str, sizeof(hour_str), "%d", current_time.tm_hour);
    snprintf(minute_str, sizeof(minute_str), "%d", current_time.tm_min);
    gtk_entry_set_text(GTK_ENTRY(hour_entry), hour_str);
    gtk_entry_set_text(GTK_ENTRY(minute_entry), minute_str);

    gtk_box_pack_start(GTK_BOX(time_hbox), hour_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(time_hbox), hour_entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(time_hbox), minute_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(time_hbox), minute_entry, FALSE, FALSE, 0);

    // Pack everything
    gtk_box_pack_start(GTK_BOX(start_time_vbox), start_now_radio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(start_time_vbox), start_specific_radio, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(start_time_vbox), time_hbox, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(start_time_frame), start_time_vbox);

    gtk_box_pack_start(GTK_BOX(content), interval_label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), interval_entry, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), start_time_frame, FALSE, FALSE, 5);

    gtk_widget_show_all(dialog);

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        const char* interval_text = gtk_entry_get_text(GTK_ENTRY(interval_entry));
        int interval = atoi(interval_text);

        if (interval > 0) {
            time_t start_time = 0;

            // Check if specific time is selected
            if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(start_specific_radio))) {
                const char* hour_text = gtk_entry_get_text(GTK_ENTRY(hour_entry));
                const char* minute_text = gtk_entry_get_text(GTK_ENTRY(minute_entry));
                int hour = atoi(hour_text);
                int minute = atoi(minute_text);

                // Validate time input
                if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
                    time_t now_time = time(NULL);
                    struct tm tm_info = *localtime(&now_time);
                    tm_info.tm_hour = hour;
                    tm_info.tm_min = minute;
                    tm_info.tm_sec = 0;
                    start_time = mktime(&tm_info);
                } else {
                    gtk_label_set_text(GTK_LABEL(status_label), "Invalid time format.");
                    gtk_widget_destroy(dialog);
                    return;
                }
            }

            add_reminder_to_list(interval, start_time);

            if (start_time == 0) {
                gtk_label_set_text(GTK_LABEL(status_label), "Reminder added (starting now).");
            } else {
                char time_buffer[64];
                format_time(start_time, time_buffer, sizeof(time_buffer));
                char status_buffer[128];
                snprintf(status_buffer, sizeof(status_buffer),
                         "Reminder added (starting at %s).", time_buffer);
                gtk_label_set_text(GTK_LABEL(status_label), status_buffer);
            }
        } else {
            gtk_label_set_text(GTK_LABEL(status_label), "Invalid interval.");
        }
    }
    gtk_widget_destroy(dialog);
}

void on_delete_reminder_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    if (num_reminders == 0) {
        gtk_label_set_text(GTK_LABEL(status_label), "No reminders to delete.");
        return;
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("Delete Reminder",
                                                   GTK_WINDOW(main_window),
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   "OK", GTK_RESPONSE_OK,
                                                   "Cancel", GTK_RESPONSE_CANCEL,
                                                   NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new("Enter reminder ID to delete:");
    GtkWidget *entry = gtk_entry_new();

    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(content), entry, FALSE, FALSE, 5);
    gtk_widget_show_all(dialog);

    int response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_OK) {
        const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
        int id = atoi(text);
        if (id > 0) {
            delete_reminder_by_id(id);
            gtk_label_set_text(GTK_LABEL(status_label), "Reminder deleted.");
        }
    }
    gtk_widget_destroy(dialog);
}

void on_window_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    cleanup_app();
    gtk_main_quit();
}

// --- App Initialization ---

void init_app() {
    init_reminders_array();
    init_day_logs_array();
    reminder_labels = g_ptr_array_new_with_free_func((GDestroyNotify)gtk_widget_destroy);
    get_current_date_midnight(&last_checked_date);
    load_reminders();
    load_drink_logs();
}

void cleanup_app() {
    save_drink_logs();
    free(reminders);
    reminders = NULL;
    num_reminders = 0;
    max_reminders_capacity = 0;
    free(day_logs);
    day_logs = NULL;
    num_day_logs = 0;
    max_day_logs_capacity = 0;
    current_day_index = -1;
}

// --- Main Function ---

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    init_app();

    // Create main window
    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "💧 Water Reminder");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 500, 400);
    gtk_container_set_border_width(GTK_CONTAINER(main_window), 10);
    g_signal_connect(main_window, "destroy", G_CALLBACK(on_window_destroy), NULL);

#ifdef _WIN32
    tray_init(main_window, "Water Reminder Running");
    g_signal_connect(main_window, "destroy", G_CALLBACK(tray_cleanup), NULL);
#endif

    apply_progress_bar_style();

    // Create main vertical box
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_add(GTK_CONTAINER(main_window), vbox);

    // Water status section
    water_label = gtk_label_new("💧 Today: 0 drinks, 0 oz total");
    gtk_label_set_xalign(GTK_LABEL(water_label), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), water_label, FALSE, FALSE, 0);

    // Progress bar
    progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(progress_bar), TRUE);
    gtk_widget_set_size_request(progress_bar, -1, 18);
    gtk_box_pack_start(GTK_BOX(vbox), progress_bar, FALSE, FALSE, 0);

    // Quick log buttons
    GtkWidget *button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

    GtkWidget *log_8oz_btn = gtk_button_new_with_label("Log 8 oz");
    GtkWidget *log_16oz_btn = gtk_button_new_with_label("Log 16 oz");
    GtkWidget *log_custom_btn = gtk_button_new_with_label("Log Custom");
    GtkWidget *set_goal_btn = gtk_button_new_with_label("Set Goal");

    g_signal_connect(log_8oz_btn, "clicked", G_CALLBACK(on_log_8oz_clicked), NULL);
    g_signal_connect(log_16oz_btn, "clicked", G_CALLBACK(on_log_16oz_clicked), NULL);
    g_signal_connect(log_custom_btn, "clicked", G_CALLBACK(on_log_custom_clicked), NULL);
    g_signal_connect(set_goal_btn, "clicked", G_CALLBACK(on_set_goal_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(button_box), log_8oz_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), log_16oz_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), log_custom_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(button_box), set_goal_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), button_box, FALSE, FALSE, 0);

    // Reminders section
    GtkWidget *reminders_frame = gtk_frame_new("Active Reminders");
    GtkWidget *scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                  GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, -1, 150);

    reminders_list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(scrolled), reminders_list);
    gtk_container_add(GTK_CONTAINER(reminders_frame), scrolled);
    gtk_box_pack_start(GTK_BOX(vbox), reminders_frame, TRUE, TRUE, 0);

    // Reminder buttons
    GtkWidget *reminder_button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    GtkWidget *add_reminder_btn = gtk_button_new_with_label("Add Reminder");
    GtkWidget *delete_reminder_btn = gtk_button_new_with_label("Delete Reminder");

    g_signal_connect(add_reminder_btn, "clicked", G_CALLBACK(on_add_reminder_clicked), NULL);
    g_signal_connect(delete_reminder_btn, "clicked", G_CALLBACK(on_delete_reminder_clicked), NULL);

    gtk_box_pack_start(GTK_BOX(reminder_button_box), add_reminder_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(reminder_button_box), delete_reminder_btn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), reminder_button_box, FALSE, FALSE, 0);

    // Status label
    status_label = gtk_label_new("Ready. Log water or manage reminders.");
    gtk_label_set_xalign(GTK_LABEL(status_label), 0.0);
    gtk_box_pack_start(GTK_BOX(vbox), status_label, FALSE, FALSE, 0);

    // Update displays
    update_water_display();
    update_reminders_list();
    update_progress_bar();

    // Set up timer for background checks (every 1 second)
    g_timeout_add_seconds(1, timer_callback, NULL);

    gtk_widget_show_all(main_window);
    gtk_main();

    return 0;
}
