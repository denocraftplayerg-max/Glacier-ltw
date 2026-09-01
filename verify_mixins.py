import os
import re

def parse_client_mappings(mappings_path):
    mappings = {}
    if not os.path.exists(mappings_path):
        print(f"[Aviso] Arquivo de mapeamento não encontrado: {mappings_path}")
        return mappings
    
    with open(mappings_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            parts = line.split()
            if len(parts) >= 2:
                mappings[parts[0]] = parts[1]
    return mappings

def verify_project_mixins(project_dir, mappings):
    mixin_pattern = re.compile(r'@Mixin\s*\(\s*(?:value\s*=\s*)?\{?([^}]+)\}?\s*\)')
    inject_pattern = re.compile(r'@Inject\s*\(.*?at\s*=\s*@At\([^)]*value\s*=\s*"([^"]+)"', re.DOTALL)
    
    total_mixins = 0

    print(f"Iniciando varredura em: {project_dir}\n")

    if not os.path.exists(project_dir):
        print(f"[Erro] Diretório do projeto não encontrado: {project_dir}")
        return

    for root, _, files in os.walk(project_dir):
        for file in files:
            if file.endswith('.java'):
                file_path = os.path.join(root, file)
                with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                    content = f.read()
                    
                    mixin_matches = mixin_pattern.findall(content)
                    if mixin_matches:
                        for match in mixin_matches:
                            total_mixins += 1
                            targets = re.findall(r'([a-zA-Z0-9_.]+)\.class', match)
                            for target in targets:
                                print(f"[Mixin Detectado] {file} -> Alvo: {target}")
                                
                    inject_matches = inject_pattern.findall(content)
                    for at_value in inject_matches:
                        if "HEAD" not in at_value and "TAIL" not in at_value and "INVOKE" not in at_value:
                            print(f"[Checar @At] {file}: Ponto de injeção incomum '{at_value}'")

    print(f"\nVarredura concluída. Total de blocos @Mixin encontrados: {total_mixins}")
    print(f"Mapeamentos carregados do arquivo: {len(mappings)}")

if __name__ == "__main__":
    PROJECT_SOURCE = "./src/main/java"
    MAPPINGS_FILE = "/storage/emulated/0/Download/client.txt"
    
    loaded_mappings = parse_client_mappings(MAPPINGS_FILE)
    verify_project_mixins(PROJECT_SOURCE, loaded_mappings)
