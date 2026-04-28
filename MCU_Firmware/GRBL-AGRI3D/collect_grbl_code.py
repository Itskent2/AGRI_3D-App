import os

# Configuration
OUTPUT_FILE = 'grbl_agri3d_full_code.txt'
EXTENSIONS = ('.h', '.c', '.cpp', '.ino', '.txt', '.properties')
EXCLUDE_FILES = [OUTPUT_FILE, os.path.basename(__file__)]

def collect_code():
    print(f"Collecting code from current directory only: {os.getcwd()}...")
    
    with open(OUTPUT_FILE, 'w', encoding='utf-8') as f_out:
        # Get all items in the current directory and filter for files only
        files = [f for f in os.listdir('.') if os.path.isfile(f)]
        
        # Sort files to keep output consistent
        for filename in sorted(files):
            if filename.endswith(EXTENSIONS) and filename not in EXCLUDE_FILES:
                print(f"Adding: {filename}")
                
                # Write header
                f_out.write('=' * 80 + '\n')
                f_out.write(f'FILE: {filename}\n')
                f_out.write('=' * 80 + '\n\n')
                
                try:
                    with open(filename, 'r', encoding='utf-8', errors='ignore') as f_in:
                        f_out.write(f_in.read())
                except Exception as e:
                    f_out.write(f"\n[ERROR READING FILE: {e}]\n")
                
                f_out.write('\n\n')
                    
    print(f"\nDone! All code has been saved to: {OUTPUT_FILE}")

if __name__ == '__main__':
    collect_code()