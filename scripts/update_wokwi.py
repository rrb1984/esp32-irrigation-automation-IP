Import("env")
import re
import os

# Obtenemos el nombre del entorno actual que has seleccionado abajo en VS Code
current_env = env.get("PIOENV")
toml_file = "wokwi.toml"

if os.path.exists(toml_file):
    with open(toml_file, "r") as file:
        content = file.read()

    # Reemplazamos las rutas antiguas por las del entorno actual
    content = re.sub(r'firmware\s*=\s*[\'"].*?[\'"]', f"firmware = '.pio/build/{current_env}/firmware_combined_for_wokwi.bin'", content)
    content = re.sub(r'elf\s*=\s*[\'"].*?[\'"]', f"elf = '.pio/build/{current_env}/firmware.elf'", content)

    with open(toml_file, "w") as file:
        file.write(content)
        
    print(f"\n---> [WOKWI AUTOMATION] wokwi.toml actualizado automáticamente para apuntar a: {current_env} <---\n")
else:
    print("\n---> [WOKWI AUTOMATION] No se encontró wokwi.toml <---\n")