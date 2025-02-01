This is a simple real-time CPU usage monitor written in C that works in a standard console environment. The program displays CPU usage statistics such as current usage, maximum and minimum observed usage, system load averages, system uptime, and the number of CPU cores. It does not rely on the ncurses library and can be used on any online C compiler or standard terminal.

Features
Displays real-time CPU usage in percentage.
Shows maximum and minimum CPU usage observed during the run.
Displays system load averages for 1, 5, and 15 minutes.
Displays system uptime in seconds.
Displays the number of CPU cores.
Visualizes CPU usage as a simple progress bar in the terminal.
Press 'q' to quit the program.
Requirements
C compiler (gcc or compatible)
No additional libraries are required for this version, as it uses only standard C functions and file operations.

Installation and Usage
Clone the repository or download the source code.

If you're using an online C compiler, simply paste the code into the editor.

Compile the program (if using a local compiler):

bash
Copy
Edit
gcc -o cpu_monitor cpu_monitor.c
Run the program:

bash
Copy
Edit
./cpu_monitor
Interpreting the output: The program will display CPU usage in real-time, along with load averages and uptime. The CPU usage is displayed as a percentage with a progress bar to give a visual representation.

Exit the program: Press q to quit the program.

Example Output: 
pgsql
Copy
Edit
Real-Time CPU Usage Monitor
Current CPU Usage: 23.45%
Max CPU Usage Observed: 85.67%
Min CPU Usage Observed: 10.12%
Load Averages (1/5/15 min): 0.53 / 0.68 / 0.75
System Uptime: 2314.45 seconds
Number of CPU Cores: 4
[####--------------]
Press 'q' to quit
Notes: 
This program works in environments where /proc/stat, /proc/loadavg, and /proc/uptime are available (common in Linux-based systems).
It can be run on local Linux systems or in online C compilers that provide access to these files.
License
This project is licensed under the MIT License.

AUTHOR 
ELURU POOJITH KUMAR REDDY
