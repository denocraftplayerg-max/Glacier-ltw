#!/data/data/com.termux/files/usr/bin/bash
set -e

echo "🔨 Compilando LTW com FFM API..."

# Verificar NDK
if [ -z "$ANDROID_NDK_ROOT" ]; then
    echo "❌ ANDROID_NDK_ROOT não definido. Exporte antes de compilar."
    exit 1
fi

# Compilar LTW
cd ltw
mkdir -p build
cd build

cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-21 \
    -DCMAKE_BUILD_TYPE=Release

make -j$(nproc)

echo "✓ LTW compilado: ltw/build/libltw.so"

cd ../../..

# Compilar mod Fabric
echo ""
echo "🔨 Compilando mod Fabric com FFM..."
./gradlew build

echo ""
echo "✅ Build completo!"
echo "   LTW: ltw/build/libltw.so"
echo "   Mod: build/libs/*.jar"
