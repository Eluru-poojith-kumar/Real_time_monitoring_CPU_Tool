#include <stdio.h> 
#include <stdlib.h> 
#include <unistd.h> 
#include <string.h> 
#include <ncurses.h> 
#define DELAY 1000000 

void get_cpu_times(unsigned long long *idle, unsigned long long *total) { 
FILE *fp = fopen("/proc/stat", "r"); 
if (!fp) { 
perror("Failed to read /proc/stat"); 
exit(EXIT_FAILURE); 
} 
char line[256]; 
fgets(line, sizeof(line), fp); 
fclose(fp); 
unsigned long long user, nice, system, idle_time, iowait, irq, softirq, steal; 
sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu", 
&user, &nice, &system, &idle_time, &iowait, &irq, &softirq, &steal); 
*idle = idle_time + iowait; 
*total = user + nice + system + idle_time + iowait + irq + softirq + steal; 
} 
double calculate_cpu_usage(unsigned long long prev_idle, unsigned long long 
prev_total, 
unsigned long long idle, unsigned long long total) { 
unsigned long long idle_diff = idle - prev_idle; 
unsigned long long total_diff = total - prev_total; 
return 100.0 * (1.0 - ((double)idle_diff / total_diff)); 
}
void get_system_info(double *loadavg1, double *loadavg5, double *loadavg15, double 
*uptime) { 
FILE *fp = fopen("/proc/loadavg", "r"); 
if (!fp) { 
perror("Failed to read /proc/loadavg"); 
exit(EXIT_FAILURE); 
} 
fscanf(fp, "%lf %lf %lf", loadavg1, loadavg5, loadavg15); 
fclose(fp); 
fp = fopen("/proc/uptime", "r"); 
if (!fp) { 
perror("Failed to read /proc/uptime"); 
exit(EXIT_FAILURE); 
} 
fscanf(fp, "%lf", uptime); 
fclose(fp); 
} 
 
int get_cpu_cores() { 
FILE *fp = fopen("/proc/cpuinfo", "r"); 
if (!fp) { 
perror("Failed to read /proc/cpuinfo"); 
exit(EXIT_FAILURE); 
} 
int cores = 0; 
char line[256]; 
while (fgets(line, sizeof(line), fp)) { 
if (strncmp(line, "processor", 9) == 0) { 
cores++; 
} 
} 
fclose(fp); 
return cores; 
}
int main() { 
unsigned long long prev_idle = 0, prev_total = 0; 
unsigned long long idle, total; 
double cpu_usage; 
double max_usage = 0.0, min_usage = 100.0; 
double loadavg1, loadavg5, loadavg15, uptime; 
int cpu_cores = get_cpu_cores(); 
initscr(); 
noecho(); 
cbreak(); 
timeout(0); 
curs_set(FALSE); 
while (1) { 
get_cpu_times(&idle, &total); 
cpu_usage = calculate_cpu_usage(prev_idle, prev_total, idle, total); 
prev_idle = idle; 
prev_total = total; 
if (cpu_usage > max_usage) max_usage = cpu_usage; 
if (cpu_usage < min_usage) min_usage = cpu_usage; 
get_system_info(&loadavg1, &loadavg5, &loadavg15, &uptime); 
clear(); 
mvprintw(0, 0, "Real-Time CPU Usage Monitor"); 
mvprintw(1, 0, "Current CPU Usage: %.2f%%", cpu_usage); 
mvprintw(2, 0, "Max CPU Usage Observed: %.2f%%", max_usage); 
mvprintw(3, 0, "Min CPU Usage Observed: %.2f%%", min_usage);
mvprintw(5, 0, "Load Averages (1/5/15 min): %.2f / %.2f / %.2f", loadavg1, 
loadavg5, loadavg15); 
mvprintw(6, 0, "System Uptime: %.2f seconds", uptime); 
mvprintw(7, 0, "Number of CPU Cores: %d", cpu_cores); 
int usage_bar_width = 30; 
int usage_fill = (int)((cpu_usage / 100) * usage_bar_width); 
mvprintw(9, 0, "["); 
for (int i = 0; i < usage_fill; i++) { 
mvprintw(9, i + 1, "#"); 
} 
for (int i = usage_fill; i < usage_bar_width; i++) { 
mvprintw(9, i + 1, "-"); 
} 
mvprintw(9, usage_bar_width + 1, "]"); 
mvprintw(11, 0, "Press 'q' to quit"); 
refresh(); 
usleep(DELAY); 
int ch = getch(); 
if (ch == 'q' || ch == 'Q') { 
break; 
} 
} 
endwin(); 
return 0; 
} 