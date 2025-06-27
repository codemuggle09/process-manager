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
    double cpu_percent = -1.0;
    double mem_percent = -1.0;
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

    void run() {
        std::cout << "ProcessManager::run() called successfully.\n";
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
        return std::to_string(uid);
    }

    ProcessInfo getProcessInfo(int pid) {
        ProcessInfo proc = {};
        proc.pid = pid;
        proc.cpu_percent = -1.0;
        proc.mem_percent = -1.0;

        std::ifstream stat_file("/proc/" + std::to_string(pid) + "/stat");
        if (!stat_file.is_open()) throw std::runtime_error("Could not open stat");

        std::string line;
        if (!std::getline(stat_file, line)) throw std::runtime_error("Could not read stat line");

        std::istringstream iss(line);
        std::string comm, state;
        int ppid;
        long utime, stime, starttime;

        iss >> proc.pid >> comm >> state >> ppid;
        for (int i = 0; i < 9; i++) iss.ignore(256, ' ');
        iss >> utime >> stime;
        for (int i = 0; i < 6; i++) iss.ignore(256, ' ');
        iss >> starttime;

        proc.name = comm.substr(1, comm.length() - 2);
        proc.state = state;
        proc.ppid = ppid;
        proc.utime = utime;
        proc.stime = stime;
        proc.starttime = starttime;

        std::ifstream status_file("/proc/" + std::to_string(pid) + "/status");
        if (!status_file.is_open()) throw std::runtime_error("Could not open status");

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

        long total_time = proc.utime + proc.stime;
        auto it = prev_cpu_times.find(pid);
        if (it != prev_cpu_times.end()) {
            long time_diff = total_time - it->second;
            proc.cpu_percent = (double)time_diff / sysconf(_SC_CLK_TCK) * 100.0;
        }
        prev_cpu_times[pid] = total_time;

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
                        if (!proc.name.empty() && proc.cpu_percent >= 0.0 && proc.mem_percent >= 0.0) {
                            processes.push_back(proc);
                        }
                    } catch (...) {}
                }
            }
        }
        closedir(proc_dir);

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
};

int main() {
    std::cout << "Linux Process Manager - htop Clone\n";
    std::cout << "Initializing...\n";

    ProcessManager pm;
    pm.run();

    std::cout << "Goodbye!\n";
    return 0;
}
