import os

def merge_binaries(firmware_path, firmware_addr, partitions_path, partitions_addr, output_path):
    # Convert addresses from hex string to integer
    firmware_addr = int(firmware_addr, 16)
    partitions_addr = int(partitions_addr, 16)

    # Read binary contents
    with open(firmware_path, 'rb') as f:
        firmware_data = f.read()
    with open(partitions_path, 'rb') as f:
        partitions_data = f.read()

    # Create a list of (address, data) tuples
    binaries = [
        (partitions_addr, partitions_data),
        (firmware_addr, firmware_data)
    ]

    # Sort binaries by address
    binaries.sort()

    # Create merged binary
    with open(output_path, 'wb') as out_file:
        current_address = 0
        for addr, data in binaries:
            if addr > current_address:
                # Fill gap with 0xFF
                out_file.write(b'\xFF' * (addr - current_address))
                current_address = addr
            out_file.write(data)
            current_address += len(data)

    print(f"Merged binary saved to {output_path}")

# Define paths and addresses
firmware_path = ".pio/build/esp32dev/firmware.bin"
partitions_path = ".pio/build/esp32dev/partitions.bin"
output_path = ".pio/build/esp32dev/merged_output.bin"
firmware_addr = "0x10000"
partitions_addr = "0x8000"

# Check if input files exist before merging
if os.path.exists(firmware_path) and os.path.exists(partitions_path):
    merge_binaries(firmware_path, firmware_addr, partitions_path, partitions_addr, output_path)
else:
    print("Error: One or both input files do not exist.")