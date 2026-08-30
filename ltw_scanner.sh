#!/system/bin/sh
# LTW Shader Blacklist Scanner
# Objetivo: Identificar shaders incompatíveis com o pipeline de conversão do LTW e GLES 3.0/3.1

TARGET_DIR="/storage/emulated/0/Ltw/shaders/"

if [ ! -d "$TARGET_DIR" ]; then
    echo "ERRO: Diretório '$TARGET_DIR' não encontrado."
    echo "Ajuste o caminho ou verifique as permissões de armazenamento (termux-setup-storage)."
    exit 1
fi

echo "Iniciando varredura de incompatibilidades AST/GLES em: $TARGET_DIR"
echo "================================================================"

# Regex unificada compatível com POSIX/sh do Android (Toybox)
# Cobre: Compute, Subroutines, Geometry/Tessellation outputs, SSBOs, Switch cases com macros, Bitshifts, Desktop Core, Atomics, Image Load/Store
REGEX="layout[[:space:]]*\(.*local_size|subroutine|gl_PrimitiveID|gl_Layer|gl_ViewportIndex|buffer[[:space:]]+[a-zA-Z0-9_]+[[:space:]]*\{|case[[:space:]]+[a-zA-Z_][a-zA-Z0-9_]*:|<<|>>|#version[[:space:]]+(330|4[0-9]0)[[:space:]]+core|atomic_uint|atomicCounter|image2D|image3D|imageLoad|imageStore"

# Executa a busca recursiva em arquivos de shader
RESULTS=$(grep -r -E -i -n -H "$REGEX" "$TARGET_DIR" --include=\*.vsh --include=\*.fsh --include=\*.gsh --include=\*.glsl --include=\*.comp 2>/dev/null)

if [ -z "$RESULTS" ]; then
    echo "STATUS: Nenhum padrão crítico de lista negra detectado."
else
    echo "ALERTA: Shaders candidatos à lista negra (Blacklist) encontrados."
    echo "----------------------------------------------------------------"
    echo "$RESULTS" | awk -F: '{
        file = $1;
        line = $2;
        content = "";
        for(i=3; i<=NF; i++) content = content (i==3 ? "" : ":") $i;
        gsub(/^[[:space:]]+/, "", content);
        printf "[ARQUIVO] %s\n[LINHA %s] %s\n----------------------------------------\n", file, line, content;
    }'
fi

echo "Varredura concluída."
