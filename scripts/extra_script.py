Import("env")
import os
import struct
from pathlib import Path

# --- CONFIGURACIÓN CLAVE ---
OUTPUT_BIN_NAME = "firmware_combined_for_wokwi.bin"

FLASH_MAP = [
    {"offset": "0x1000", "file": "bootloader.bin"},
    {"offset": "0x8000", "file": "partitions.bin"},
    {"offset": "0x10000", "file": "firmware.bin"},
    # FS offset is resolved dynamically from partitions.bin to avoid mismatch.
    {"offset": "auto", "file": "littlefs.bin"},
]
# --------------------------


def resolve_filesystem_offset(build_dir: Path):
    partitions_path = build_dir / "partitions.bin"
    if not partitions_path.exists():
        return None

    data = partitions_path.read_bytes()
    entry_size = 32
    for i in range(0, len(data), entry_size):
        entry = data[i : i + entry_size]
        if len(entry) < entry_size:
            break

        magic = entry[0:2]
        if magic == b"\xff\xff":
            break
        if magic != b"\xaaP":
            continue

        p_type = entry[2]
        p_subtype = entry[3]
        offset = struct.unpack_from("<I", entry, 4)[0]
        size = struct.unpack_from("<I", entry, 8)[0]

        # 0x01 = data, 0x82 = spiffs/littlefs/fat partition subtype on ESP32.
        if p_type == 0x01 and p_subtype == 0x82:
            return {"offset": offset, "size": size}

    return None

def merge_firmware(source, target, env):
    print("-" * 60)
    print("PlatformIO Post-Build: Fusionando archivos binarios para Wokwi...")
    
    build_dir = Path(env.subst("$BUILD_DIR"))
    output_path = build_dir / OUTPUT_BIN_NAME
    
    chip = env.get('BOARD_MCU', 'esp32')
    flash_mode = 'dio'
    flash_size = env.get('BOARD_FLASH_SIZE', '4MB')
    
    python_exe = env.subst('$PYTHONEXE')
    
    # --- EL ARREGLO ESTÁ AQUÍ ---
    # Buscamos la ruta exacta donde PlatformIO guarda la herramienta esptool
    esptool_dir = env.PioPlatform().get_package_dir("tool-esptoolpy")
    esptool_py = os.path.join(esptool_dir, "esptool.py")
    
    # Ejecutamos el script esptool.py directamente usando su ruta absoluta
    cmd = f'"{python_exe}" "{esptool_py}" --chip {chip} merge_bin -o "{output_path}" --flash_mode {flash_mode} --flash_size {flash_size}'
    # ----------------------------

    fs_partition = resolve_filesystem_offset(build_dir)
    if fs_partition:
        print(
            f"Sistema de archivos detectado en offset 0x{fs_partition['offset']:X} "
            f"(tamano 0x{fs_partition['size']:X})"
        )
    else:
        print("ADVERTENCIA: No se pudo detectar particion FS en partitions.bin.")

    archivos_encontrados = 0
    for item in FLASH_MAP:
        file_path = build_dir / item['file']
        if file_path.exists():
            offset = item["offset"]
            if offset == "auto":
                if fs_partition:
                    file_size = file_path.stat().st_size
                    if file_size > fs_partition["size"]:
                        print(
                            "ERROR: littlefs.bin excede el tamano de la particion FS "
                            f"(0x{file_size:X} > 0x{fs_partition['size']:X})."
                        )
                        env.Exit(1)
                    offset = f"0x{fs_partition['offset']:X}"
                else:
                    print(
                        "ADVERTENCIA: Usando offset de respaldo 0x330000 para littlefs.bin."
                    )
                    offset = "0x330000"

            cmd += f' {offset} "{file_path}"'
            archivos_encontrados += 1
        else:
            if item["file"] == "littlefs.bin":
                print(
                    "ERROR: littlefs.bin no encontrado. Ejecuta primero: "
                    "platformio run -e wokwi -t buildfs"
                )
                env.Exit(1)
            print(f"ADVERTENCIA: Archivo esencial '{item['file']}' no encontrado. Omitiendo.")

    if archivos_encontrados == 0:
        print("ERROR: No se encontraron archivos binarios para fusionar. Abortando.")
        return
    
    print("Ejecutando esptool.py...")
    
    # Ejecutar el comando
    exit_code = env.Execute(cmd)
    
    if exit_code == 0:
        print(f"¡Éxito! Archivo combinado creado en: {output_path}")
    else:
        print("ERROR: Falló la ejecución de esptool.py. Revisa la salida superior.")
        env.Exit(1)

    print("-" * 60)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge_firmware)