// cpu_monitor.c
// Compile: gcc cpu_monitor.c -o cpu_monitor -lncurses
// Run: sudo ./cpu_monitor   (log file location may require permissions)

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ncurses.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/stat.h>

#define DELAY_US 500000            // 0.5 seconds between samples
#define ALERT_THRESHOLD 80.0       // CPU % above which an alert is triggered
#define LOG_FILE "cpu_monitor.log"
#define LOG_MAX_BYTES (1024 * 1024) // rotate when log > 1MB
#define SEND_ALERTS 1              // 1 to enable UDP alert forwarding, 0 to disable
#define SERVER_IP "127.0.0.1"      // Alert server (set to your monitoring server)
#define SERVER_PORT 9999           // Alert server port

static volatile int keep_running = 1;
static FILE *logf = NULL;
static int udp_sock = -1;
static struct sockaddr_in server_addr;

// forward declarations
void handle_signal(int sig);
void open_log();
void close_log();
void rotate_log_if_needed();
void write_log(const char *fmt, ...);
int get_cpu_cores();
void get_cpu_times(unsigned long long *idle, unsigned long long *total, int *ok);
double calculate_cpu_usage(unsigned long long prev_idle, unsigned long long prev_total, unsigned long long idle, unsigned long long total, int ok);
void get_system_info(double *loadavg1, double *loadavg5, double *loadavg15, double *uptime, int *ok);
void send_udp_alert(const char *message);
const char* timestamp_now();

void handle_signal(int sig) {
    keep_running = 0;
}

void open_log() {
    if (!logf) {
        logf = fopen(LOG_FILE, "a");
        if (!logf) {
            // fallback to stderr but continue running
            fprintf(stderr, "Warning: could not open log file '%s': %s\n", LOG_FILE, strerror(errno));
        } else {
            setvbuf(logf, NULL, _IOLBF, 0); // line buffered
        }
    }
}

void close_log() {
    if (logf) {
        fclose(logf);
        logf = NULL;
    }
}

void rotate_log_if_needed() {
    if (!logf) return;
    // Get file size
    long size = 0;
    struct stat st;
    if (stat(LOG_FILE, &st) == 0) {
        size = st.st_size;
    } else {
        return;
    }
    if (size < LOG_MAX_BYTES) return;

    // Close, rename, and reopen
    fclose(logf);
    logf = NULL;

    // create rotated filename with timestamp
    char rotated[512];
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    snprintf(rotated, sizeof(rotated), "%s.%04d%02d%02d_%02d%02d%02d",
             LOG_FILE,
             tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    if (rename(LOG_FILE, rotated) != 0) {
        // rename may fail; try unlinking and continue
        fprintf(stderr, "Warning: could not rotate log file: %s\n", strerror(errno));
    }
    // reopen a fresh log
    open_log();
    if (logf) {
        fprintf(logf, "%s Log rotated: previous file moved to %s\n", timestamp_now(), rotated);
    }
}

void write_log(const char *fmt, ...) {
    open_log();
    rotate_log_if_needed();
    if (!logf) return;
    va_list ap;
    va_start(ap, fmt);
    fprintf(logf, "%s ", timestamp_now());
    vfprintf(logf, fmt, ap);
    fprintf(logf, "\n");
    va_end(ap);
    fflush(logf);
}

int get_cpu_cores() {
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) return 1;
    int cores = 0;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "processor", 9) == 0) cores++;
    }
    fclose(fp);
    return (cores > 0) ? cores : 1;
}

/*
 * Reads /proc/stat and extracts CPU times. If it fails, sets ok=0.
 */
void get_cpu_times(unsigned long long *idle, unsigned long long *total, int *ok) {
    *ok = 0;
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) {
        write_log("Warning: Failed to open /proc/stat: %s", strerror(errno));
        return;
    }
    char line[512];
    if (!fgets(line, sizeof(line), fp)) {
        write_log("Warning: Failed to read /proc/stat");
        fclose(fp);
        return;
    }
    fclose(fp);

    unsigned long long user=0, nice=0, system=0, idle_time=0, iowait=0, irq=0, softirq=0, steal=0;
    // Some kernels may not provide all fields; use sscanf return count to be safe
    int cnt = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                     &user, &nice, &system, &idle_time, &iowait, &irq, &softirq, &steal);
    if (cnt < 4) {
        write_log("Warning: Unexpected /proc/stat format");
        return;
    }
    *idle = idle_time + iowait;
    *total = user + nice + system + idle_time + iowait + irq + softirq + steal;
    *ok = 1;
}

/*
 * Returns CPU usage percent. If ok==0 (cannot compute), returns 0.0.
 * Handles first iteration where prev_total == 0.
 */
double calculate_cpu_usage(unsigned long long prev_idle, unsigned long long prev_total, unsigned long long idle, unsigned long long total, int ok) {
    if (!ok) return 0.0;
    if (total <= prev_total || prev_total == 0) {
        // can't compute meaningful delta yet
        return 0.0;
    }
    unsigned long long idle_diff = idle - prev_idle;
    unsigned long long total_diff = total - prev_total;
    if (total_diff == 0) return 0.0;
    double usage = 100.0 * (1.0 - ((double)idle_diff / (double)total_diff));
    if (usage < 0.0) usage = 0.0;
    if (usage > 100.0) usage = 100.0;
    return usage;
}

void get_system_info(double *loadavg1, double *loadavg5, double *loadavg15, double *uptime, int *ok) {
    *ok = 0;
    FILE *fp = fopen("/proc/loadavg", "r");
    if (!fp) {
        write_log("Warning: Failed to open /proc/loadavg: %s", strerror(errno));
    } else {
        if (fscanf(fp, "%lf %lf %lf", loadavg1, loadavg5, loadavg15) < 1) {
            write_log("Warning: /proc/loadavg unexpected format");
        }
        fclose(fp);
    }
    fp = fopen("/proc/uptime", "r");
    if (!fp) {
        write_log("Warning: Failed to open /proc/uptime: %s", strerror(errno));
        return;
    }
    if (fscanf(fp, "%lf", uptime) != 1) {
        write_log("Warning: /proc/uptime unexpected format");
        fclose(fp);
        return;
    }
    fclose(fp);
    *ok = 1;
}

void send_udp_alert(const char *message) {
#if SEND_ALERTS
    if (udp_sock < 0) return;
    size_t len = strlen(message);
    ssize_t sent = sendto(udp_sock, message, len, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (sent < 0) {
        write_log("Warning: UDP send failed: %s", strerror(errno));
    } else {
        write_log("Sent UDP alert (%zd bytes): %s", (ssize_t)sent, message);
    }
#endif
}

const char* timestamp_now() {
    static char buf[64];
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm = localtime(&tv.tv_sec);
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03ld",
             tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec, tv.tv_usec/1000);
    return buf;
}

int main() {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // Prepare UDP socket if enabled
#if SEND_ALERTS
    udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_sock < 0) {
        fprintf(stderr, "Warning: could not create UDP socket: %s\n", strerror(errno));
        // continue without network alerts
        udp_sock = -1;
    } else {
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(SERVER_PORT);
        if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
            fprintf(stderr, "Warning: invalid SERVER_IP '%s'\n", SERVER_IP);
            close(udp_sock);
            udp_sock = -1;
        }
    }
#endif

    open_log();
    write_log("Starting CPU monitor");

    int cpu_cores = get_cpu_cores();

    // ncurses init
    initscr();
    noecho();
    cbreak();
    timeout(0); // non-blocking getch
    curs_set(FALSE);

    unsigned long long prev_idle = 0ULL, prev_total = 0ULL;
    unsigned long long idle = 0ULL, total = 0ULL;
    double cpu_usage = 0.0;
    double max_usage = 0.0, min_usage = 100.0;
    double loadavg1 = 0.0, loadavg5 = 0.0, loadavg15 = 0.0, uptime = 0.0;
    int ok_times = 0, ok_sys = 0;
    int cycle = 0;

    while (keep_running) {
        get_cpu_times(&idle, &total, &ok_times);
        double usage = calculate_cpu_usage(prev_idle, prev_total, idle, total, ok_times);

        // update previous for next cycle (always update to current if ok)
        if (ok_times) {
            prev_idle = idle;
            prev_total = total;
        }

        // compute system info
        get_system_info(&loadavg1, &loadavg5, &loadavg15, &uptime, &ok_sys);

        // on first cycle usage may be 0; we keep showing it
        cpu_usage = usage;
        if (cpu_usage > max_usage) max_usage = cpu_usage;
        if (cpu_usage < min_usage) min_usage = cpu_usage;

        // write to log every cycle (or you can throttle)
        write_log("CPU: %.2f%% | Max: %.2f | Min: %.2f | Loadavg: %.2f/%.2f/%.2f | Uptime: %.2f s",
                  cpu_usage, max_usage, min_usage, loadavg1, loadavg5, loadavg15, uptime);

        // render ncurses UI
        clear();
        mvprintw(0, 0, "Real-Time CPU Usage Monitor (PID %d)", getpid());
        mvprintw(1, 0, "Current CPU Usage: %.2f%%", cpu_usage);
        mvprintw(2, 0, "Max CPU Usage Observed: %.2f%%", max_usage);
        mvprintw(3, 0, "Min CPU Usage Observed: %.2f%%", min_usage);
        mvprintw(5, 0, "Load Averages (1/5/15 min): %.2f / %.2f / %.2f", loadavg1, loadavg5, loadavg15);
        mvprintw(6, 0, "System Uptime: %.2f seconds", uptime);
        mvprintw(7, 0, "Number of CPU Cores: %d", cpu_cores);

        int usage_bar_width = 40;
        int usage_fill = (int)((cpu_usage / 100.0) * usage_bar_width);
        if (usage_fill < 0) usage_fill = 0;
        if (usage_fill > usage_bar_width) usage_fill = usage_bar_width;

        mvprintw(9, 0, "[");
        for (int i = 0; i < usage_fill; ++i) mvprintw(9, i + 1, "#");
        for (int i = usage_fill; i < usage_bar_width; ++i) mvprintw(9, i + 1, "-");
        mvprintw(9, usage_bar_width + 1, "]");

        // alerting logic
        if (cpu_usage >= ALERT_THRESHOLD) {
            attron(A_BOLD);
            mvprintw(11, 0, "ALERT: CPU Usage Above %.1f%%", ALERT_THRESHOLD);
            attroff(A_BOLD);
            // send UDP alert (non-blocking)
            char alert_msg[512];
            snprintf(alert_msg, sizeof(alert_msg), "%s ALERT CPU %.2f%% load %.2f/%.2f/%.2f",
                     timestamp_now(), cpu_usage, loadavg1, loadavg5, loadavg15);
            write_log("ALERT triggered: %s", alert_msg);
            send_udp_alert(alert_msg);
        } else {
            mvprintw(11, 0, "Status: OK");
        }

        mvprintw(13, 0, "Press 'q' to quit. Cycle: %d", cycle++);
        refresh();

        // check user input
        int ch = getch();
        if (ch == 'q' || ch == 'Q') {
            keep_running = 0;
            break;
        }

        // sleep
        usleep(DELAY_US);
    }

    // cleanup
    endwin();
    write_log("Shutting down CPU monitor");
    close_log();
#if SEND_ALERTS
    if (udp_sock >= 0) close(udp_sock);
#endif
    return 0;
}
