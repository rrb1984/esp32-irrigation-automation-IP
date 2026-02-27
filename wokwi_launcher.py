Import("env")
import subprocess
import socket
import os
import time

# --- AJUSTA TU RUTA AQUÍ ---
GATEWAY_EXE = r"C:\wokwigw\wokwigw.exe"

print("\n" + "="*40)
print(">>> INICIANDO WOKWI LAUNCHER SCRIPT")
print("="*40 + "\n")

def check_and_launch():
    # Comprobar si el puerto 9011 está ocupado
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        is_running = s.connect_ex(('127.0.0.1', 9011)) == 0
    
    if not is_running:
        if os.path.exists(GATEWAY_EXE):
            print(f"[+] Lanzando Gateway desde: {GATEWAY_EXE}")
            subprocess.Popen([GATEWAY_EXE], creationflags=subprocess.CREATE_NEW_CONSOLE)
            time.sleep(1) # Esperar a que abra
        else:
            print(f"[!] ERROR: No se encuentra el Gateway en la ruta especificada.")
    else:
        print("[OK] Wokwi Gateway ya está funcionando.")

check_and_launch()