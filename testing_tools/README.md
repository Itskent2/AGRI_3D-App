# AGRI-3D Automated Thesis Tester

This tool connects directly to the ESP32 via WebSocket to automate your testing trials. 
It generates random coordinates within the workable dimensions of the machine, moves the gantry, pauses to let you manually measure the physical distance, and saves the target vs actual distances to a `.csv` file.

## Setup Instructions

1. **Install Python**: Make sure Python is installed on your laptop.
2. **Install Dependencies**: Open a terminal in this `testing_tools` folder and run:
   ```bash
   pip install -r requirements.txt
   ```
   *(This installs the `websockets` library required to talk to the ESP32)*

## How to Run

1. Connect your laptop to the **AGRI_3D** Wi-Fi network (or whatever network the ESP32 is on).
2. Run the script from the terminal:
   ```bash
   python thesis_tester.py
   ```
3. Enter the IP address of your ESP32 (default is `192.168.4.1`).
4. Select which test you want to run (Isolated X, Isolated Y, Concurrent XY, Isolated Z).
5. Enter the number of trials (default `100`).

## How it works

- The script pulls the maximum dimensions directly from the GRBL settings (`$130`, `$131`, `$132`).
- It strictly enforces the rule that **Z is isolated** and must fully retract before any X or Y movement occurs.
- After each movement, the script will pause and prompt you to input the actual physical measurement.
- Type the measurement into the terminal and press Enter.
- The trial is immediately saved to a `results_[TEST]_[TIMESTAMP].csv` file. This file can be imported directly into Excel or SPSS for your thesis analysis!
