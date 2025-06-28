lines = []
commands = []

with open("log.log", "r") as f:
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
    lines[i] = lines[i].replace(f"[{command}]", "").strip()
    
    # find the command id (inside () brackets)
    if "(" in command and ")" in command:
      start = command.index("(") + 1
      end = command.index(")")
      command_id = command[start:end]
      commands.append({
        "id": int(command_id),
        "value": [int(x, 16) for x in lines[i].split(" ")]
      })
  
def print_buffer(buffer):
  for byte in buffer:
    print(f"{chr(byte)}", end="")
  print()
  
buffer = []
for command in commands:
  id = command["id"]
  if id == 5:
    v = command["value"]
    offset = v[0]
    
    if offset == len(buffer):
      buffer.extend(v[1:])
    else:
      print(f"Error: Offset {offset} does not match buffer length {len(buffer)}")
  elif id == 6:
    v = command["value"]
    offset = v[0] << 8 | v[1]
    if offset == len(buffer):
      buffer.extend(v[2:])
    else:
      print(f"Error: Offset {offset} does not match buffer length {len(buffer)}")
  
  elif id == 7:
    print_buffer(buffer)
    buffer = []
  elif id == 8:
    v = command["value"]
    print_buffer(v[2:])