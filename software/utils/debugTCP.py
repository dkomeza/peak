with open("tcp.log", "r") as f:
    lines = f.readlines()
    
for i in range(len(lines)):
    lines[i] = lines[i].strip()
    
    # Remove the first part up to the " > " if it exists
    if " > " in lines[i]:
        lines[i] = lines[i].split(" > ", 1)[-1]
        
    # Find the command part inside [] brackets
    if "[" in lines[i] and "]" in lines[i]:
        start = lines[i].index("[") + 1
        end = lines[i].index("]")
        command = lines[i][start:end]
        
        # Remove the command part from the line
        lines[i] = lines[i].replace(f"[{command}]", "").strip().split(" ")[2:]
        # remove the last 3 bytes
        lines[i] = lines[i][:-3]
        # convert the remaining hex values to integers
        lines[i] = [int(x, 16) for x in lines[i]]
        
        
print(lines)

def print_buffer(buffer):
    for byte in buffer:
        print(f"{chr(byte)}", end="")
    print()

for line in lines:
  print_buffer(line)