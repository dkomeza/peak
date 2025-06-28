import os

lines = []
commands = []

with open("upload.log", "r") as f:
    lines = f.readlines()
    
for i in range(len(lines)):
  lines[i] = lines[i].strip()

  # Find the command part inside [] brackets
  if "[" in lines[i] and "]" in lines[i]:
      start = lines[i].index("[") + 1
      end = lines[i].index("]")
      command = lines[i][start:end]
      
      # Remove the command part from the line
      lines[i] = lines[i].replace(f"[{command}]", "").strip().split(" ")
      
      commands.append({
        "id": int(command, 16) >> 8,
        "value": lines[i][:] 
      })
    

def dump_buffer(buffer):
  with open("upload-pretty.log", "a") as f:
    s = "-" * 40 + "\n"
    command = int(buffer[0], 16)
    s += f"Command: "
    command_str = ""
    if command == 0x00:
      command_str = "FW Version"
    elif command == 0x01:
      command_str = "JUMP BOOTLOADER"
    elif command == 0x02:
      command_str = "Erase NEW APP"
    elif command == 0x03:
      command_str = "Write NEW APP DATA"
    elif command == 73:
      command_str = "Erase BOOTLOADER"
    else:
      command_str = f"{command}"
    s += f"{command_str}, data: "
    s += " ".join(buffer[1:]) + "\n"
    f.write(s)

os.remove("upload-pretty.log")

buffer = []
for command in commands:
  id = command["id"]
  if id == 5:
    v = command["value"]
    offset = int(v[0], 16)
    
    if offset == len(buffer):
      buffer.extend(v[1:])
    else:
      print(f"Error: Offset {offset} does not match buffer length {len(buffer)}")
  elif id == 6:
    v = command["value"]
    offset = int(v[0],16) << 8 | int(v[1], 16)
    if offset == len(buffer):
      buffer.extend(v[2:])
    else:
      print(f"Error: Offset {offset} does not match buffer length {len(buffer)}")
  
  elif id == 7:
    dump_buffer(buffer)
    buffer = []
  elif id == 8:
    v = command["value"]
    dump_buffer(v[2:])
      