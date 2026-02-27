Import("env")
import os
from pathlib import Path

# --- CONFIGURACIÓN CLAVE ---
OUTPUT_BIN_NAME = "firmware_combined_for_wokwi.bin"

FLASH_MAP = [
    {"offset": "0x1000", "file": "bootloader.bin"},
    {"offset": "0x8000", "file": "partitions.bin"},
    {"offset": "0x10000", "file": "firmware.bin"},
]
# --------------------------

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

    archivos_encontrados = 0
    for item in FLASH_MAP:
        file_path = build_dir / item['file']
        if file_path.exists():
            cmd += f' {item["offset"]} "{file_path}"'
            archivos_encontrados += 1
        else:
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