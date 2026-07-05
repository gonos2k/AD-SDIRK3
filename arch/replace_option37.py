with open('configure.defaults', 'r') as f:
    lines = f.readlines()

# Find option 37 start (line 1833)
start_idx = 1833
end_idx = start_idx

# Find the next ARCH section
for i in range(start_idx + 1, len(lines)):
    if lines[i].startswith('#ARCH'):
        end_idx = i - 1
        break

# Read the new option 37 content
with open('option37_sdirk3.txt', 'r') as f:
    new_content = f.readlines()

# Replace the section
new_lines = lines[:start_idx] + new_content + ['\n\n'] + lines[end_idx+1:]

# Write back
with open('configure.defaults', 'w') as f:
    f.writelines(new_lines)

print(f"Replaced option 37 from line {start_idx+1} to {end_idx+1}")