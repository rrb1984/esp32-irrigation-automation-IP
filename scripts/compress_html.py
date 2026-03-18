#!/usr/bin/env python3
import gzip
import os
import sys

# Rutas
script_dir = os.path.dirname(os.path.abspath(__file__))
project_dir = os.path.dirname(script_dir)
source_file = os.path.join(project_dir, 'html', 'index_en.html')
target_file = os.path.join(project_dir, 'data', 'index_en.html.gz')

try:
    # Leer el HTML
    with open(source_file, 'r', encoding='utf-8') as f:
        html_content = f.read()
    
    # Minimizar: remover espacios y saltos de línea innecesarios
    import re
    minified = re.sub(r'\s+', ' ', html_content)
    minified = re.sub(r'>\s*<', '><', minified)
    minified = re.sub(r'=\s*"', '="', minified)
    minified = minified.strip()
    
    # Comprimir con gzip
    with gzip.open(target_file, 'wb') as gf:
        gf.write(minified.encode('utf-8'))
    
    # Cálculos de tamaño
    orig_size = len(html_content.encode('utf-8')) / 1024
    comp_size = os.path.getsize(target_file) / 1024
    ratio = (comp_size / orig_size) * 100
    
    print(f'✓ Generado exitosamente:')
    print(f'  Formateado: html/index_en.html ({orig_size:.2f} KB)')
    print(f'  Comprimido: data/index_en.html.gz ({comp_size:.2f} KB)')
    print(f'  Ratio: {ratio:.2f}%')
    sys.exit(0)
    
except Exception as e:
    print(f'✗ Error: {e}')
    sys.exit(1)
