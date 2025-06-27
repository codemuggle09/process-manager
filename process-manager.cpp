#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <iomanip>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <chrono>
#include <thread>

struct ProcessInfo {
    int pid;
    std::string name;
    std::string state;
    int ppid;
    double cpu_percent;
    double mem_percent;
    long mem_rss;  // Resident Set Size in KB
    long mem_vms;  // Virtual Memory Size in KB
    std::string user;
    long utime, stime;  // CPU times
    long starttime;
};

struct SystemInfo {
    double cpu_percent;
    long total_mem;
    long used_mem;
    long free_mem;
    int total_processes;
    int running_processes;
};

class ProcessManager {
private:
    std::vector<ProcessInfo> processes;
    SystemInfo sys_info;
    std::map<int, long> prev_cpu_times;
    long prev_total_cpu_time;
    int sort_column;
    bool reverse_sort;
    
public:
    ProcessManager() : sort_column(4), reverse_sort(true), prev_total_cpu_time(0) {}
    
    void clearScreen() {
        std::cout << "\033[2J\033[H";
    }
    
    void hideCursor() {
        std::cout << "\033[?25l";
    }
    
    void showCursor() {
        std::cout << "\033[?25h";
    }
    
    long getTotalCpuTime() {
        std::ifstream file("/proc/stat");
        std::string line;
        if (std::getline(file, line)) {
            std::istringstream iss(line);
            std::string cpu;
            long user, nice, system, idle, iowait, irq, softirq, steal;
            iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
            return user + nice + system + idle + iowait + irq + softirq + steal;
        }
        return 0;
    }
    
    SystemInfo getSystemInfo() {
        SystemInfo info = {};
        
        // Get memory information
        std::ifstream meminfo("/proc/meminfo");
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.find("MemTotal:") == 0) {
                std::istringstream iss(line);
                std::string label;
                iss >> label >> info.total_mem;
            } else if (line.find("MemAvailable:") == 0) {
                std::istringstream iss(line);
                std::string label;
                long available;
                iss >> label >> available;
                info.used_mem = info.total_mem - available;
                info.free_mem = available;
            }
        }
        
        // Get CPU usage
        std::ifstream stat("/proc/stat");
        if (std::getline(stat, line)) {
            std::istringstream iss(line);
            std::string cpu;
            long user, nice, system, idle, iowait, irq, softirq, steal;
            iss >> cpu >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
            
            long total = user + nice + system + idle + iowait + irq + softirq + steal;
            long work = user + nice + system;
            
            if (prev_total_cpu_time > 0) {
                long total_diff = total - prev_total_cpu_time;
                long work_diff = work - (prev_total_cpu_time - idle);
                info.cpu_percent = total_diff > 0 ? (double)work_diff / total_diff * 100.0 : 0.0;
            }
            prev_total_cpu_time = total;
        }
        
        return info;
    }
    
    std::string getUserName(int uid) {
        // Simplified - in real implementation, you'd look up /etc/passwd
        return std::to_string(uid);
    }
    
    ProcessInfo getProcessInfo(int pid) {
        ProcessInfo proc = {};
        proc.pid = pid;
        
        // Read /proc/[pid]/stat
        std::ifstream stat_file("/proc/" + std::to_string(pid) + "/stat");
        std::string line;
        if (std::getline(stat_file, line)) {
            std::istringstream iss(line);
            std::string comm, state;
            int ppid;
            long utime, stime, starttime;
            
            iss >> proc.pid >> comm >> state >> ppid;
            for (int i = 0; i < 9; i++) iss.ignore(256, ' '); // skip fields
            iss >> utime >> stime;
            for (int i = 0; i < 6; i++) iss.ignore(256, ' '); // skip more fields
            iss >> starttime;
            
            proc.name = comm.substr(1, comm.length() - 2); // remove parentheses
            proc.state = state;
            proc.ppid = ppid;
            proc.utime = utime;
            proc.stime = stime;
            proc.starttime = starttime;
        }
        
        // Read /proc/[pid]/status for memory info
        std::ifstream status_file("/proc/" + std::to_string(pid) + "/status");
        while (std::getline(status_file, line)) {
            if (line.find("VmRSS:") == 0) {
                std::istringstream iss(line);
                std::string label;
                iss >> label >> proc.mem_rss;
            } else if (line.find("VmSize:") == 0) {
                std::istringstream iss(line);
                std::string label;
                iss >> label >> proc.mem_vms;
            } else if (line.find("Uid:") == 0) {
                std::istringstream iss(line);
                std::string label;
                int uid;
                iss >> label >> uid;
                proc.user = getUserName(uid);
            }
        }
        
        // Calculate CPU percentage
        long total_time = proc.utime + proc.stime;
        auto it = prev_cpu_times.find(pid);
        if (it != prev_cpu_times.end()) {
            long time_diff = total_time - it->second;
            proc.cpu_percent = (double)time_diff / sysconf(_SC_CLK_TCK) * 100.0;
        }
        prev_cpu_times[pid] = total_time;
        
        // Calculate memory percentage
        if (sys_info.total_mem > 0) {
            proc.mem_percent = (double)proc.mem_rss / sys_info.total_mem * 100.0;
        }
        
        return proc;
    }
    
    void scanProcesses() {
        processes.clear();
        sys_info = getSystemInfo();
        
        DIR* proc_dir = opendir("/proc");
        if (!proc_dir) return;
        
        struct dirent* entry;
        while ((entry = readdir(proc_dir)) != nullptr) {
            if (entry->d_type == DT_DIR) {
                std::string name = entry->d_name;
                if (std::all_of(name.begin(), name.end(), ::isdigit)) {
                    int pid = std::stoi(name);
                    try {
                        ProcessInfo proc = getProcessInfo(pid);
                        if (!proc.name.empty()) {
                            processes.push_back(proc);
                        }
                    } catch (...) {
                        // Process might have disappeared
                    }
                }
            }
        }
        closedir(proc_dir);
        
        // Update system info
        sys_info.total_processes = processes.size();
        sys_info.running_processes = std::count_if(processes.begin(), processes.end(),
            [](const ProcessInfo& p) { return p.state == "R"; });
    }
    
    void sortProcesses() {
        std::sort(processes.begin(), processes.end(), [this](const ProcessInfo& a, const ProcessInfo& b) {
            bool result;
            switch (sort_column) {
                case 0: result = a.pid < b.pid; break;
                case 1: result = a.name < b.name; break;
                case 2: result = a.state < b.state; break;
                case 3: result = a.user < b.user; break;
                case 4: result = a.cpu_percent < b.cpu_percent; break;
                case 5: result = a.mem_percent < b.mem_percent; break;
                case 6: result = a.mem_rss < b.mem_rss; break;
                default: result = a.cpu_percent < b.cpu_percent; break;
            }
            return reverse_sort ? !result : result;
        });
    }
    
    void printHeader() {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        int width = w.ws_col;
        
        // System info header
        std::cout << "CPU: " << std::fixed << std::setprecision(1) << sys_info.cpu_percent << "%  ";
        std::cout << "Mem: " << sys_info.used_mem/1024 << "MB/" << sys_info.total_mem/1024 << "MB  ";
        std::cout << "Processes: " << sys_info.total_processes << " total, " << sys_info.running_processes << " running\n";
        
        // CPU bar
        int bar_width = 50;
        int filled = (int)(sys_info.cpu_percent / 100.0 * bar_width);
        std::cout << "CPU [";
        for (int i = 0; i < bar_width; i++) {
            if (i < filled) std::cout << "█";
            else std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1) << sys_info.cpu_percent << "%\n";
        
        // Memory bar
        double mem_usage = (double)sys_info.used_mem / sys_info.total_mem * 100.0;
        filled = (int)(mem_usage / 100.0 * bar_width);
        std::cout << "Mem [";
        for (int i = 0; i < bar_width; i++) {
            if (i < filled) std::cout << "█";
            else std::cout << " ";
        }
        std::cout << "] " << std::fixed << std::setprecision(1) << mem_usage << "%\n\n";
        
        // Column headers
        std::cout << std::left
                  << std::setw(8) << "PID"
                  << std::setw(16) << "NAME"
                  << std::setw(6) << "STATE"
                  << std::setw(8) << "USER"
                  << std::setw(8) << "CPU%"
                  << std::setw(8) << "MEM%"
                  << std::setw(10) << "RSS(KB)"
                  << "COMMAND\n";
        
        std::cout << std::string(width, '-') << "\n";
    }
    
    void printProcesses() {
        struct winsize w;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
        int max_rows = w.ws_row - 8; // Leave space for header
        
        for (int i = 0; i < std::min(max_rows, (int)processes.size()); i++) {
            const ProcessInfo& proc = processes[i];
            
            std::cout << std::left
                      << std::setw(8) << proc.pid
                      << std::setw(16) << proc.name.substr(0, 15)
                      << std::setw(6) << proc.state
                      << std::setw(8) << proc.user.substr(0, 7)
                      << std::setw(8) << std::fixed << std::setprecision(1) << proc.cpu_percent
                      << std::setw(8) << std::fixed << std::setprecision(1) << proc.mem_percent
                      << std::setw(10) << proc.mem_rss
                      << proc.name << "\n";
        }
    }
    
    void printInstructions() {
        std::cout << "\nControls: q=quit, c=sort by CPU, m=sort by memory, p=sort by PID, n=sort by name, r=reverse\n";
    }
    
    void display() {
        scanProcesses();
        sortProcesses();
        
        clearScreen();
        printHeader();
        printProcesses();
        printInstructions();
    }
    
    void setSortColumn(int col) {
        if (sort_column == col) {
            reverse_sort = !reverse_sort;
        } else {
            sort_column = col;
            reverse_sort = (col == 4 || col == 5); // Default descending for CPU and memory
        }
    }
    
    bool killProcess(int pid) {
        return kill(pid, SIGTERM) == 0;
    }
    
    void run() {
        hideCursor();
        
        // Set terminal to non-blocking mode
        struct termios old_termios, new_termios;
        tcgetattr(STDIN_FILENO, &old_termios);
        new_termios = old_termios;
        new_termios.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
        
        bool running = true;
        while (running) {
            display();
            
            // Check for input (non-blocking)
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);
            struct timeval timeout = {1, 0}; // 1 second timeout
            
            if (select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &timeout) > 0) {
                char ch;
                if (read(STDIN_FILENO, &ch, 1) > 0) {
                    switch (ch) {
                        case 'q':
                        case 'Q':
                            running = false;
                            break;
                        case 'c':
                        case 'C':
                            setSortColumn(4); // CPU
                            break;
                        case 'm':
                        case 'M':
                            setSortColumn(5); // Memory
                            break;
                        case 'p':
                        case 'P':
                            setSortColumn(0); // PID
                            break;
                        case 'n':
                        case 'N':
                            setSortColumn(1); // Name
                            break;
                        case 'r':
                        case 'R':
                            reverse_sort = !reverse_sort;
                            break;
                        case 'k':
                        case 'K':
                            // Kill process functionality
                            std::cout << "\nEnter PID to kill: ";
                            showCursor();
                            tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
                            int pid;
                            std::cin >> pid;
                            if (killProcess(pid)) {
                                std::cout << "Process " << pid << " terminated.\n";
                            } else {
                                std::cout << "Failed to terminate process " << pid << ".\n";
                            }
                            std::cout << "Press any key to continue...";
                            std::cin.get();
                            std::cin.get();
                            tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
                            hideCursor();
                            break;
                    }
                }
            }
            
            // Small delay to prevent excessive CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Restore terminal settings
        tcsetattr(STDIN_FILENO, TCSANOW, &old_termios);
        showCursor();
        clearScreen();
    }
};

int main() {
    std::cout << "Linux Process Manager - htop Clone\n";
    std::cout << "Initializing...\n";
    
    ProcessManager pm;
    pm.run();
    
    std::cout << "Goodbye!\n";
    return 0;
}