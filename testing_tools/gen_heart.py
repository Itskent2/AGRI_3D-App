import numpy as np
import matplotlib.pyplot as plt

def generate_heart_coords(num_points=500, scale=1.0):
    """Generates x, y coordinates for a heart shape."""
    t = np.linspace(0, 2 * np.pi, num_points)
    
    # Parametric equations for a heart shape
    x = 16 * np.sin(t)**3
    y = 13 * np.cos(t) - 5 * np.cos(2*t) - 2 * np.cos(3*t) - np.cos(4*t)
    
    # Apply scale to make it bigger or smaller
    x = x * scale
    y = y * scale
    
    return x, y

def plot_heart():
    """Plots the generated heart shape using matplotlib."""
    print("Generating heart coordinates...")
    x, y = generate_heart_coords(num_points=500, scale=1.0)
    
    plt.figure(figsize=(8, 6))
    plt.plot(x, y, color='red', linewidth=2)
    plt.fill(x, y, color='red', alpha=0.3)
    
    plt.title('Heart Shape (XY Axis)')
    plt.xlabel('X Axis')
    plt.ylabel('Y Axis')
    plt.grid(True, linestyle='--', alpha=0.7)
    
    # Set equal aspect ratio so the heart doesn't look stretched
    plt.axis('equal') 
    
    # Display the plot
    plt.show()

def generate_gcode(filename='heart.gcode', scale=1.0, feedrate=1000):
    """Generates basic GRBL G-code for drawing the heart shape."""
    # Use fewer points for G-code so it doesn't stutter on the machine
    x, y = generate_heart_coords(num_points=100, scale=scale)
    
    # Center at X500, Y500
    x_center = (np.max(x) + np.min(x)) / 2
    y_center = (np.max(y) + np.min(y)) / 2
    x = x - x_center + 500
    y = y - y_center + 500
    
    print(f"Generating G-code to {filename}...")
    with open(filename, 'w') as f:
        f.write("; Heart Shape G-code\n")
        f.write("G21 ; Set units to millimeters\n")
        f.write("G90 ; Absolute positioning\n")
        
        # 1. Home the X
        f.write("$HX ; Home X-axis\n")
        
        # Safe Z clearance before moving
        f.write("G0 Z20 ; 20mm Z-clearance\n")
        
        # 2. Go to the center
        f.write("G0 X500 Y500 ; Go to center\n")
        
        # Move to starting point of the heart
        f.write(f"G0 X{x[0]:.2f} Y{y[0]:.2f}\n")
        
        # 3. Draw a heart
        f.write(f"G1 Z0 F{feedrate} ; Lower pen/tool\n")
        for i in range(1, len(x)):
            f.write(f"G1 X{x[i]:.2f} Y{y[i]:.2f} F{feedrate}\n")
        
        f.write("G0 Z20 ; Raise pen/tool\n")
        
        # 4. Go to center
        f.write("G0 X500 Y500 ; Return to center\n")
        
        # 5. Then homes
        f.write("$H ; Home all axes\n")
        
    print(f"G-code successfully saved to '{filename}'")

if __name__ == "__main__":
    # Generate G-code for the GRBL machine
    # Scale of 12.5 makes the 32-unit wide heart 400mm wide
    generate_gcode(filename='heart_test.gcode', scale=12.5, feedrate=2000)
