# Tetris-Project
A modern implementation of classic Tetris with real-time global leaderboard support, allowing multiple players to compete and share scores across different game instances.

         ✨ Features

🎯 Classic Tetris gameplay with smooth controls

🌐 Real-time global leaderboard shared across all players

🔄 Multi-client support - multiple players can compete simultaneously

⚡ Automatic score synchronization - no manual refresh needed

🎮 Intuitive ncurses-based interface

📊 Live leaderboard display during gameplay

         🏗️ System Architecture


┌─────────────────┐    ┌─────────────────────┐    ┌─────────────────┐
│   Tetris        │    │   Leaderboard       │    │   Tetris        │
│   Client        │◄──►│   Server            │◄──►│   Client        │
│                 │    │                     │    │                 │
│ • Game Logic    │    │ • Global Scores     │    │ • Game Logic    │
│ • Local Display │    │ • Multi-client      │    │ • Local Display │
│ • Network Comm  │    │   Handling          │    │ • Network Comm  │
└─────────────────┘    └─────────────────────┘    └─────────────────┘
         ▲                         ▲                         ▲
         │                         │                         │
    ┌────┴─────┐             ┌─────┴─────┐             ┌─────┴─────┐
    │ Terminal │             │ TCP Port  │             │ Terminal  │
    │  Window  │             │   8080    │             │  Window   │
    └──────────┘             └───────────┘             └───────────┘


         📦 Installation
    
Prerequisites

GCC compiler

ncurses library

Linux/Unix environment

Step-by-Step Setup

1. Clone the repository

   git clone https://github.com/yourusername/tetris-global-leaderboard.git
   cd tetris-global-leaderboard

2. Make the setup script executable
   chmod +x run_tetris.sh

3. Run the automated setup
   ./run_tetris.sh

This script will:

Compile both server and client

Detect your IP address automatically

Configure network settings

Provide ready-to-run commands

         🚀 Running the Game

Terminal 1 - Start the Leaderboard Server
  ./leaderboard_server

Expected Output:
  Leaderboard Server started on port 8080
  Waiting for connections...

Terminal 2 - Start the Tetris Game
  ./tetris

Terminal 3+ (Optional) - Additional Players
  ./tetris

         🎮 Controls

Key	   Action
← →	   Move piece left/right
↑	     Rotate piece clockwise
↓	     Soft drop (move down faster)
Space	 Hard drop (instant drop)
P	     Pause game
Q	     Quit game

         🔧 Manual Compilation

Compile the Leaderboard Server
    gcc -o leaderboard_server leaderboard_server.c
Compile the Tetris Client
    gcc -o tetris tetris.c tetris_network.c -lncurses -lm -lpthread

         🌐 Network Configuration

The project automatically configures network settings. For manual configuration, edit tetris_network.h:
  #define SERVER_IP "192.168.1.100"  // Replace with your server IP
  #define SERVER_PORT 8080

         📊 Leaderboard Features

Real-time top 10 scores display

Automatic score submission on game over

Live updates across all connected clients

Player identification system

Persistent scoring during server runtime

         🐛 Troubleshooting
Common Issues & Solutions

Server won't start:

# Check if port 8080 is available
netstat -tulpn | grep 8080

# Kill existing processes
pkill leaderboard_server

Connection issues:
# Test server connectivity
telnet YOUR_SERVER_IP 8080

# Check firewall settings
sudo ufw status

Compilation errors:
# Install ncurses if missing
sudo apt-get install libncurses5-dev

# Ensure all files are present
ls -la *.c *.h

Game display issues:
# Ensure terminal supports ncurses
echo $TERM

# Try resizing terminal window
# Minimum 80x24 recommended

Debug Mode
Run server with logging:
  ./leaderboard_server > server.log 2>&1 &
  tail -f server.log

         🗂️ Project Structure
tetris-global-leaderboard/
├── tetris.c                 # Main game logic and rendering
├── tetris_network.c         # Network communication handling
├── tetris_network.h         # Network constants and prototypes
├── leaderboard_server.c     # TCP server for global leaderboard
├── run_tetris.sh           # Automated build and setup script
└── README.md               # Project documentation

         🎯 Technical Details

Protocol: Custom TCP-based communication

Port: 8080 (configurable)

Max Clients: Limited by system resources

Data Format: Simple text-based protocol

Threading: Multi-threaded server handling

         🙏 Acknowledgments

Inspired by classic Tetris game

Built with ncurses for terminal graphics

Socket programming for network functionality



