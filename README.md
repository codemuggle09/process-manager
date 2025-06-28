# Linux Process Manager - A Simple `htop` Clone

A minimal `htop`-like command-line process manager built in C++ for Linux systems.  
It reads system and process information from the `/proc` filesystem and displays a real-time summary of running processes, CPU usage, memory consumption, and other system-level statistics.

## Features

- Displays a list of active processes  
- Shows PID, command, state, memory usage, and priority  
- Parses data from the Linux `/proc` virtual filesystem  
- Lightweight and minimal dependencies  
- Designed for Linux (works in WSL too)  

## Project Structure

```
process-manager/
├── bin/                     # Compiled binary (after build)
├── include/
│   └── parser.h             # Header for parsing utilities
├── src/
│   └── process-manager.cpp  # Main logic for process manager
├── Makefile                 # Build configuration
└── README.md                # Project documentation
```

## Installation & Usage

### Prerequisites

- Linux or WSL (Windows Subsystem for Linux)
- g++ (C++11 or higher)
- make utility

### Build Instructions

```bash
git clone https://github.com/codemuggle09/process-manager.git
cd process-manager
make
```

This will compile the code and generate the executable in the `bin/` directory.

### Run the Program

```bash
./bin/process_manager
```

## How It Works

This process manager accesses and parses the virtual `/proc` filesystem, which contains detailed runtime information about system processes. The following files are used:

- `/proc/[pid]/status` – For retrieving process metadata  
- `/proc/[pid]/cmdline` – To get the command-line arguments  
- `/proc/[pid]/stat` – For state, priority, and memory information  

The data is processed using standard C++ file handling mechanisms (`ifstream`, `sstream`, etc.).

## Clean Build

To remove compiled binaries and object files:

```bash
make clean
```

## Author

Monisha B.  
GitHub: [codemuggle09](https://github.com/codemuggle09)

## License

This project is open-source and licensed under the [MIT License](LICENSE).

## Contributions

Contributions are welcome. Feel free to fork the project and open a pull request with your improvements, bug fixes, or suggestions.

## Future Improvements

- Add CPU and memory usage bars  
- Refresh the view dynamically (like `htop`)  
- Add unit tests for parser functions  
- Implement support to kill or renice processes
