#!/bin/bash
set -e

echo "════════════════════════════════════════════════════════════"
echo "  CAFFEINE ENGINE - FULL REBUILD"
echo "════════════════════════════════════════════════════════════"
echo ""

# 1. Clean previous build
echo "[1/5] Limpando compilação anterior..."
rm -rf build/
mkdir -p build
cd build

# 2. CMake configure
echo "[2/5] Configurando CMake..."
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCAFFEINE_ENABLE_SCRIPTING=ON \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 3. Compile everything
echo "[3/5] Compilando caffeine-core..."
make -j$(nproc) caffeine-core

echo "[4/5] Compilando aplicações (doppio, caf-encode, etc)..."
make -j$(nproc)

echo "[5/5] Compilando testes..."
make -j$(nproc) tests

# Summary
echo ""
echo "════════════════════════════════════════════════════════════"
echo "  ✓ COMPILAÇÃO CONCLUÍDA"
echo "════════════════════════════════════════════════════════════"
ls -lh doppio caf-encode libcaffeine-core.a 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'
echo ""
echo "Binários:"
echo "  • doppio (editor)"
echo "  • caf-encode (asset compiler)"
echo "  • caffeine-combined (standalone)"
echo ""
echo "Bibliotecas:"
echo "  • libcaffeine-core.a (engine core)"
echo ""
