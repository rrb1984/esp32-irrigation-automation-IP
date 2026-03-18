const fs = require('fs');
const zlib = require('zlib');
const path = require('path');

const projectDir = path.resolve(__dirname, '..');
const sourceFile = path.join(projectDir, 'html', 'index_en.html');
const targetFile = path.join(projectDir, 'data', 'index_en.html.gz');

try {
  // Leer HTML
  let html = fs.readFileSync(sourceFile, 'utf8');
  
  // Minimizar: remover espacios y saltos de línea innecesarios
  html = html.replace(/\s+/g, ' ').replace(/>\s*</g, '><').replace(/=\s*"/g, '="').trim();
  
  // Comprimir con gzip
  const buffer = Buffer.from(html, 'utf8');
  const compressed = zlib.gzipSync(buffer);
  
  // Guardar archivo comprimido
  fs.writeFileSync(targetFile, compressed);
  
  // Estadísticas
  const origSize = (Buffer.byteLength(html, 'utf8') / 1024).toFixed(2);
  const compSize = (compressed.length / 1024).toFixed(2);
  const ratio = ((compressed.length / Buffer.byteLength(html, 'utf8')) * 100).toFixed(2);
  
  console.log('✓ Generado exitosamente:');
  console.log(`  Formateado: html/index_en.html (${origSize} KB)`);
  console.log(`  Comprimido: data/index_en.html.gz (${compSize} KB)`);
  console.log(`  Ratio: ${ratio}%`);
  process.exit(0);
} catch (err) {
  console.error('✗ Error:', err.message);
  process.exit(1);
}
