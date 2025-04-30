import os
import re
import matplotlib.pyplot as plt
import numpy as np
import subprocess

# Create assets directory if it doesn't exist
os.makedirs('assets', exist_ok=True)

def parse_execution_cycles(filename):
    """Extract execution cycles from simulation output file"""
    with open(filename, 'r') as f:
        content = f.read()
    
    # Find all execution cycle counts
    execution_cycles = []
    for core_match in re.finditer(r'Core \d+ Statistics:.*?Total Execution Cycles: (\d+)', content, re.DOTALL):
        execution_cycles.append(int(core_match.group(1)))
    
    # Return the maximum execution cycles across all cores
    return max(execution_cycles) if execution_cycles else 0

def run_simulation(trace_prefix, s, b, E, output_file):
    """Run the cache simulation with the given parameters"""
    cmd = f"./bin/L1simulate -t assignment3_traces/{trace_prefix} -s {s} -b {b} -E {E} -o {output_file}"
    subprocess.run(cmd, shell=True, check=True)
    return parse_execution_cycles(output_file)

def generate_graphs():
    """Generate graphs for varying s, E, and b parameters"""
    apps = ['app1', 'app2']
    default_s, default_E, default_b = 6, 2, 5
    
    # Parameter variations
    s_values = [default_s-1, default_s, default_s+1, default_s+2]  # 5, 6, 7, 8
    E_values = [default_E-1, default_E, default_E+1, default_E+2]  # 1, 2, 3, 4
    b_values = [default_b-1, default_b, default_b+1, default_b+2]  # 4, 5, 6, 7
    
    # Dictionary to store results
    results = {app: {'s': {}, 'E': {}, 'b': {}} for app in apps}
    
    # Run simulations for each app and parameter configuration
    for app in apps:
        # Vary set index bits (s)
        for s in s_values:
            output_file = f"temp_{app}_s{s}.txt"
            results[app]['s'][s] = run_simulation(app, s, default_b, default_E, output_file)
        
        # Vary associativity (E)
        for E in E_values:
            output_file = f"temp_{app}_E{E}.txt"
            results[app]['E'][E] = run_simulation(app, default_s, default_b, E, output_file)
        
        # Vary block bits (b)
        for b in b_values:
            output_file = f"temp_{app}_b{b}.txt"
            results[app]['b'][b] = run_simulation(app, default_s, b, default_E, output_file)
    
    # Create graphs
    for app in apps:
        # Graph for varying s
        plt.figure(figsize=(10, 6))
        s_sorted = sorted(results[app]['s'].keys())
        cycles = [results[app]['s'][s] for s in s_sorted]
        plt.plot(s_sorted, cycles, marker='o', linestyle='-', linewidth=2)
        plt.title(f'{app}: Effect of Set Index Bits (s) on Execution Cycles')
        plt.xlabel('Set Index Bits (s)')
        plt.ylabel('Maximum Execution Cycles')
        plt.grid(True)
        plt.savefig(f'assets/{app}_s_variation.png')
        plt.close()
        
        # Graph for varying E
        plt.figure(figsize=(10, 6))
        E_sorted = sorted(results[app]['E'].keys())
        cycles = [results[app]['E'][E] for E in E_sorted]
        plt.plot(E_sorted, cycles, marker='o', linestyle='-', linewidth=2)
        plt.title(f'{app}: Effect of Associativity (E) on Execution Cycles')
        plt.xlabel('Associativity (E)')
        plt.ylabel('Maximum Execution Cycles')
        plt.grid(True)
        plt.savefig(f'assets/{app}_E_variation.png')
        plt.close()
        
        # Graph for varying b
        plt.figure(figsize=(10, 6))
        b_sorted = sorted(results[app]['b'].keys())
        cycles = [results[app]['b'][b] for b in b_sorted]
        plt.plot(b_sorted, cycles, marker='o', linestyle='-', linewidth=2)
        plt.title(f'{app}: Effect of Block Bits (b) on Execution Cycles')
        plt.xlabel('Block Bits (b)')
        plt.ylabel('Maximum Execution Cycles')
        plt.grid(True)
        plt.savefig(f'assets/{app}_b_variation.png')
        plt.close()
    
    # Clean up temporary files
    for app in apps:
        for s in s_values:
            if os.path.exists(f"temp_{app}_s{s}.txt"):
                os.remove(f"temp_{app}_s{s}.txt")
        for E in E_values:
            if os.path.exists(f"temp_{app}_E{E}.txt"):
                os.remove(f"temp_{app}_E{E}.txt")
        for b in b_values:
            if os.path.exists(f"temp_{app}_b{b}.txt"):
                os.remove(f"temp_{app}_b{b}.txt")

if __name__ == "__main__":
    generate_graphs()
    print("Graphs generated and saved in the 'assets' folder")