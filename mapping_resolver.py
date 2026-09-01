#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
GLACIER MAPPING RESOLVER
========================

Resolve automaticamente erros encontrados pelo:
    mapping_auditor.py

Entrada:
    - glacier_mapping_report.json
    - client.txt
    - código Java do projeto

Funções:
    * Resolve classes oficiais
    * Resolve inner classes
    * Resolve owners de @At(INVOKE)
    * Resolve métodos
    * Compara descriptors JVM
    * Reconhece classes JDK/external
    * Analisa @Inject, @Redirect, @ModifyArg, @ModifyVariable,
      @ModifyConstant, @Overwrite, @Shadow, @Accessor e @Invoker
    * Produz sugestões de correção
    * Produz patch .diff opcional
    * Não altera o código original por padrão

Uso:

python3 mapping_resolver.py \
    --project . \
    --mapping /storage/emulated/0/Download/client.txt \
    --report glacier_mapping_report.json

Gerar patch:

python3 mapping_resolver.py \
    --project . \
    --mapping /storage/emulated/0/Download/client.txt \
    --report glacier_mapping_report.json \
    --patch glacier_mapping_fixes.diff
"""

import argparse
import difflib
import json
import os
import re
import sys
from collections import defaultdict


# ============================================================================
# CONSTANTES
# ============================================================================

PRIMITIVES = {
    "void": "V",
    "boolean": "Z",
    "byte": "B",
    "char": "C",
    "short": "S",
    "int": "I",
    "long": "J",
    "float": "F",
    "double": "D",
}

JAVA_LANG = {
    "java.lang.Object",
    "java.lang.String",
    "java.lang.Thread",
    "java.lang.Runnable",
    "java.lang.Boolean",
    "java.lang.Byte",
    "java.lang.Character",
    "java.lang.Short",
    "java.lang.Integer",
    "java.lang.Long",
    "java.lang.Float",
    "java.lang.Double",
    "java.lang.Enum",
    "java.lang.Class",
    "java.lang.System",
    "java.lang.Math",
    "java.lang.Exception",
    "java.lang.RuntimeException",
    "java.lang.Error",
    "java.lang.Throwable",
    "java.lang.AutoCloseable",
}

# ============================================================================
# UTILITÁRIOS
# ============================================================================

def normalize_class_name(name):
    """
    Normaliza nomes usados por Mixins.

    Exemplo:

        a.b.Outer.Inner
        a.b.Outer$Inner
        Outer.Inner

    Não tenta adivinhar pacotes aqui.
    """
    name = name.strip()
    name = name.replace("/", ".")

    # JVM inner-class notation
    name = name.replace("$", ".")

    return name


def jvm_class(name):
    """
    Converte:

        java.lang.String
        com.foo.Bar$Baz

    para:

        Ljava/lang/String;
        Lcom/foo/Bar$Baz;
    """

    name = name.replace(".", "/")

    # Último separador de inner class deve permanecer $
    # se já veio dessa forma.
    return "L" + name + ";"


def clean_descriptor(desc):
    if not desc:
        return None

    desc = desc.strip()

    if desc.startswith("("):
        return desc

    return desc


def descriptor_from_type(type_name):
    type_name = type_name.strip()

    # Remove annotations
    type_name = re.sub(r"@\w+(?:\([^)]*\))?\s*", "", type_name)

    # Arrays
    if type_name.endswith("[]"):
        dimensions = 0
        while type_name.endswith("[]"):
            dimensions += 1
            type_name = type_name[:-2].strip()

        return "[" * dimensions + descriptor_from_type(type_name)

    if type_name in PRIMITIVES:
        return PRIMITIVES[type_name]

    # varargs
    if type_name.endswith("..."):
        return "[" + descriptor_from_type(type_name[:-3])

    # Generic
    type_name = re.sub(r"<.*>", "", type_name)

    return "L" + type_name.replace(".", "/") + ";"


def method_descriptor(parameters, return_type):
    if parameters is None:
        parameters = []

    params = []

    for p in parameters:
        p = p.strip()

        if not p:
            continue

        params.append(descriptor_from_type(p))

    return "(" + "".join(params) + ")" + descriptor_from_type(return_type)


def split_method_descriptor(desc):
    """
    (DDDDDD)I
        ->
    ["D","D","D","D","D","D"], "I"
    """

    if not desc or not desc.startswith("("):
        return [], None

    end = desc.find(")")

    if end < 0:
        return [], None

    body = desc[1:end]
    ret = desc[end + 1:]

    params = []

    i = 0

    while i < len(body):
        c = body[i]

        if c == "[":
            start = i
            while i < len(body) and body[i] == "[":
                i += 1

            if i < len(body) and body[i] == "L":
                end_class = body.find(";", i)
                if end_class == -1:
                    return [], ret
                i = end_class + 1
            else:
                i += 1

            params.append(body[start:i])
            continue

        if c == "L":
            end_class = body.find(";", i)

            if end_class == -1:
                return [], ret

            params.append(body[i:end_class + 1])
            i = end_class + 1
            continue

        params.append(c)
        i += 1

    return params, ret


def descriptor_parameter_count(desc):
    params, _ = split_method_descriptor(desc)
    return len(params)


def line_number_from_source(text, position):
    return text.count("\n", 0, position) + 1


def similarity(a, b):
    """
    Similaridade simples sem dependências externas.
    """
    return difflib.SequenceMatcher(
        None,
        a.lower(),
        b.lower()
    ).ratio()


# ============================================================================
# MODELO DE MAPPING
# ============================================================================

class MappingDB:

    def __init__(self):
        self.classes = {}
        self.reverse_classes = {}

        self.fields = defaultdict(list)
        self.methods = defaultdict(list)

        self.method_by_owner_name = defaultdict(list)

        self.class_count = 0
        self.field_count = 0
        self.method_count = 0

    # ------------------------------------------------------------------------

    def add_class(self, official, obfuscated):
        official = official.strip()
        obfuscated = obfuscated.strip()

        self.classes[official] = obfuscated
        self.reverse_classes[obfuscated] = official

        self.class_count += 1

    # ------------------------------------------------------------------------

    def add_field(self, owner, field_type, official, obfuscated):
        record = {
            "owner": owner,
            "type": field_type,
            "name": official,
            "obfuscated": obfuscated,
        }

        self.fields[(owner, official)].append(record)
        self.field_count += 1

    # ------------------------------------------------------------------------

    def add_method(
        self,
        owner,
        start,
        end,
        return_type,
        name,
        parameters,
        obfuscated
    ):
        descriptor = method_descriptor(parameters, return_type)

        record = {
            "owner": owner,
            "start": start,
            "end": end,
            "return_type": return_type,
            "name": name,
            "parameters": parameters,
            "obfuscated": obfuscated,
            "descriptor": descriptor,
        }

        self.methods[(owner, name)].append(record)
        self.method_by_owner_name[owner].append(record)

        self.method_count += 1

    # ------------------------------------------------------------------------

    def class_exists(self, name):
        if name in self.classes:
            return True

        normalized = normalize_class_name(name)

        if normalized in self.classes:
            return True

        # Inner class:
        # Outer.Inner
        # ->
        # Outer$Inner
        parts = normalized.split(".")

        for i in range(len(parts), 0, -1):
            candidate = ".".join(parts[:i])

            if candidate in self.classes:
                return True

            if i < len(parts):
                outer = ".".join(parts[:i])
                inner = "$".join(parts[i - 1:])

                candidate = outer + "$" + inner

                if candidate in self.classes:
                    return True

        return False

    # ------------------------------------------------------------------------

    def resolve_class(self, name):
        """
        Retorna o nome oficial existente no mapping.
        """

        if name in self.classes:
            return name

        normalized = normalize_class_name(name)

        if normalized in self.classes:
            return normalized

        # tentativa inner class
        parts = normalized.split(".")

        for split in range(len(parts), 0, -1):

            outer = ".".join(parts[:split])

            if outer not in self.classes:
                continue

            if split == len(parts):
                return outer

            inner = "$".join(parts[split:])

            candidate = outer + "$" + inner

            if candidate in self.classes:
                return candidate

        # busca por sufixo
        candidates = []

        for cls in self.classes:
            if cls.endswith("." + normalized):
                candidates.append(cls)

            if cls.endswith("$" + normalized.replace(".", "$")):
                candidates.append(cls)

        if len(candidates) == 1:
            return candidates[0]

        if candidates:
            candidates.sort(key=lambda x: similarity(x, normalized), reverse=True)
            return candidates[0]

        return None

    # ------------------------------------------------------------------------

    def find_methods(self, owner, name):
        owner = self.resolve_class(owner) or owner

        return list(self.methods.get((owner, name), []))

    # ------------------------------------------------------------------------

    def find_method(self, owner, name, descriptor=None):
        candidates = self.find_methods(owner, name)

        if not candidates:
            return None, []

        if descriptor:

            exact = [
                m for m in candidates
                if m["descriptor"] == descriptor
            ]

            if exact:
                return exact[0], candidates

        return candidates[0], candidates

    # ------------------------------------------------------------------------

    def search_method_globally(self, name):
        result = []

        for (owner, method_name), methods in self.methods.items():
            if method_name == name:
                result.extend(methods)

        return result

    # ------------------------------------------------------------------------

    def find_field(self, owner, name):
        owner = self.resolve_class(owner) or owner
        return list(self.fields.get((owner, name), []))


# ============================================================================
# PARSER PROGUARD
# ============================================================================

def parse_proguard(path):
    db = MappingDB()

    current_class = None

    class_pattern = re.compile(
        r"^([^\s]+)\s+->\s+([^\s:]+):$"
    )

    member_pattern = re.compile(
        r"^\s+(.+?)\s+->\s+([^\s]+)$"
    )

    method_pattern = re.compile(
        r"^\s*"
        r"(?:(\d+):(\d+):)?"
        r"(.+?)\s+"
        r"([a-zA-Z_$<>][\w$<>]*)"
        r"\((.*?)\)"
        r"\s+->\s+([^\s]+)"
        r"$"
    )

    field_pattern = re.compile(
        r"^\s+"
        r"(.+?)\s+"
        r"([a-zA-Z_$][\w$]*)"
        r"\s+->\s+([^\s]+)"
        r"$"
    )

    with open(
        path,
        "r",
        encoding="utf-8",
        errors="ignore"
    ) as f:

        for raw in f:

            line = raw.rstrip("\n")

            if not line or line.startswith("#"):
                continue

            cm = class_pattern.match(line)

            if cm:
                current_class = cm.group(1)
                obf = cm.group(2)

                db.add_class(
                    current_class,
                    obf
                )

                continue

            if current_class is None:
                continue

            mm = method_pattern.match(line)

            if mm:

                start = mm.group(1)
                end = mm.group(2)
                return_type = mm.group(3).strip()
                method_name = mm.group(4).strip()
                params_raw = mm.group(5).strip()
                obfuscated = mm.group(6).strip()

                parameters = []

                if params_raw:
                    parameters = [
                        p.strip()
                        for p in params_raw.split(",")
                    ]

                db.add_method(
                    current_class,
                    int(start) if start else None,
                    int(end) if end else None,
                    return_type,
                    method_name,
                    parameters,
                    obfuscated
                )

                continue

            fm = field_pattern.match(line)

            if fm:

                field_type = fm.group(1).strip()
                field_name = fm.group(2).strip()
                obfuscated = fm.group(3).strip()

                db.add_field(
                    current_class,
                    field_type,
                    field_name,
                    obfuscated
                )

                continue

    return db


# ============================================================================
# JAVA SCANNER
# ============================================================================

class JavaFile:

    def __init__(self, path):
        self.path = path

        with open(
            path,
            "r",
            encoding="utf-8",
            errors="ignore"
        ) as f:
            self.text = f.read()

        self.lines = self.text.splitlines()


class Resolver:

    def __init__(self, project, db):
        self.project = project
        self.db = db

        self.java_files = []

        self.class_context = {}

        self.results = []

        self.load_java()

    # ------------------------------------------------------------------------

    def load_java(self):

        for root, dirs, files in os.walk(self.project):

            # não varrer build/cache pesado
            dirs[:] = [
                d for d in dirs
                if d not in {
                    ".git",
                    "build",
                    ".gradle",
                    ".idea",
                    "node_modules",
                    "target",
                }
            ]

            for filename in files:

                if not filename.endswith(".java"):
                    continue

                path = os.path.join(root, filename)

                try:
                    jf = JavaFile(path)
                    self.java_files.append(jf)

                except Exception:
                    pass

    # ------------------------------------------------------------------------

    def package_of(self, text):
        m = re.search(
            r"\bpackage\s+([\w.]+)\s*;",
            text
        )

        return m.group(1) if m else None

    # ------------------------------------------------------------------------

    def imports_of(self, text):
        result = {}

        for m in re.finditer(
            r"\bimport\s+(?:static\s+)?([\w.$]+)\s*;",
            text
        ):
            full = m.group(1)
            simple = full.split(".")[-1]
            result[simple] = full

        return result

    # ------------------------------------------------------------------------

    def resolve_java_type(self, type_name, jf):
        """
        Resolve simples como:

            Frustum
            Thread
            SectionRenderDispatcher.RenderSection

        para nomes completos.
        """

        type_name = type_name.strip()

        type_name = re.sub(
            r"<.*>",
            "",
            type_name
        )

        if type_name in JAVA_LANG:
            return type_name

        imports = self.imports_of(jf.text)

        simple = type_name.split(".")[0]

        if simple in imports:

            base = imports[simple]

            suffix = type_name[len(simple):]

            if suffix.startswith("."):
                return base + suffix

            return base

        package = self.package_of(jf.text)

        if package:
            candidate = package + "." + type_name

            if self.db.class_exists(candidate):
                return candidate

        if self.db.class_exists(type_name):
            return self.db.resolve_class(type_name)

        return type_name

    # ------------------------------------------------------------------------

    def is_external(self, owner):
        return (
            owner.startswith("java.")
            or owner.startswith("javax.")
            or owner.startswith("jdk.")
            or owner.startswith("sun.")
            or owner.startswith("org.w3c.")
        )

    # ------------------------------------------------------------------------

    def descriptor_from_at(self, at_text):
        m = re.search(
            r'\bdesc\s*=\s*"([^"]+)"',
            at_text
        )

        if m:
            return m.group(1)

        return None

    # ------------------------------------------------------------------------

    def parse_at(self, content):
        """
        Captura @At(...), inclusive quando possui:
            target =
            value =
            desc =
            args =
        """

        result = []

        pattern = re.compile(
            r"@At\s*\(",
            re.MULTILINE
        )

        for m in pattern.finditer(content):

            start = m.start()
            pos = m.end()

            depth = 1
            quote = False
            escaped = False

            while pos < len(content) and depth:

                c = content[pos]

                if escaped:
                    escaped = False
                    pos += 1
                    continue

                if c == "\\":
                    escaped = True
                    pos += 1
                    continue

                if c == '"':
                    quote = not quote

                elif not quote:

                    if c == "(":
                        depth += 1

                    elif c == ")":
                        depth -= 1

                pos += 1

            raw = content[start:pos]

            result.append(
                (
                    line_number_from_source(content, start),
                    raw
                )
            )

        return result

    # ------------------------------------------------------------------------

    def parse_annotation_value(self, text, key):
        m = re.search(
            rf'\b{re.escape(key)}\s*=\s*"([^"]*)"',
            text
        )

        return m.group(1) if m else None

    # ------------------------------------------------------------------------

    def extract_owner_method(self, target):
        """
        Converte:

            Lfoo/Bar;method()V

        em:

            foo.Bar
            method
            ()V
        """

        if not target:
            return None, None, None

        m = re.match(
            r"^L([^;]+);([^(]+)(\(.*\).*)$",
            target
        )

        if not m:
            return None, None, None

        owner = m.group(1).replace("/", ".")
        name = m.group(2)
        desc = m.group(3)

        return owner, name, desc

    # ------------------------------------------------------------------------

    def suggest_descriptor(self, candidates, requested):
        if not candidates:
            return None

        if not requested:
            return candidates[0]

        req_params, req_ret = split_method_descriptor(
            requested
        )

        best = None
        best_score = -1

        for candidate in candidates:

            cand_desc = candidate["descriptor"]

            cand_params, cand_ret = split_method_descriptor(
                cand_desc
            )

            score = 0

            if len(req_params) == len(cand_params):
                score += 50

            for a, b in zip(req_params, cand_params):
                if a == b:
                    score += 10

            if req_ret == cand_ret:
                score += 50

            # mesmo número de parâmetros, retorno diferente
            if (
                len(req_params) == len(cand_params)
                and req_ret != cand_ret
            ):
                score += 5

            if score > best_score:
                best_score = score
                best = candidate

        return best

    # ------------------------------------------------------------------------

    def resolve_invoke(self, jf, line, at_text):

        target = self.parse_annotation_value(
            at_text,
            "target"
        )

        desc = self.descriptor_from_at(at_text)

        value = self.parse_annotation_value(
            at_text,
            "value"
        )

        # somente INVOKE
        if value != "INVOKE":
            return None

        owner, method, target_desc = self.extract_owner_method(
            target
        )

        if target_desc and not desc:
            desc = target_desc

        if not owner:
            return {
                "type": "AT_INVOKE",
                "status": "UNRESOLVED",
                "line": line,
                "reason": "Não foi possível interpretar o target de @At(INVOKE).",
            }

        # ------------------------------------------------------------
        # JDK / external
        # ------------------------------------------------------------

        if self.is_external(owner):

            return {
                "type": "EXTERNAL_OWNER",
                "status": "EXTERNAL",
                "line": line,
                "owner": owner,
                "method": method,
                "descriptor": desc,
                "reason": (
                    "Owner pertence à API Java/JDK e não deve "
                    "ser procurado no client.txt."
                ),
            }

        resolved_owner = self.db.resolve_class(owner)

        if not resolved_owner:

            return {
                "type": "OWNER_NOT_FOUND",
                "status": "ERROR",
                "line": line,
                "owner": owner,
                "method": method,
                "descriptor": desc,
                "reason": (
                    "Owner não encontrado no mapping."
                ),
            }

        candidates = self.db.find_methods(
            resolved_owner,
            method
        )

        if not candidates:

            global_candidates = self.db.search_method_globally(
                method
            )

            return {
                "type": "METHOD_NOT_FOUND",
                "status": "ERROR",
                "line": line,
                "owner": resolved_owner,
                "method": method,
                "descriptor": desc,
                "candidates_global": global_candidates[:10],
                "reason": (
                    "Método não encontrado no owner informado."
                ),
            }

        exact = None

        if desc:
            for candidate in candidates:
                if candidate["descriptor"] == desc:
                    exact = candidate
                    break

        if exact:

            return {
                "type": "RESOLVED",
                "status": "OK",
                "line": line,
                "owner": resolved_owner,
                "method": method,
                "requested_descriptor": desc,
                "resolved_descriptor": exact["descriptor"],
                "mapping": exact,
                "reason": "Owner, método e descriptor correspondem.",
            }

        best = self.suggest_descriptor(
            candidates,
            desc
        )

        return {
            "type": "DESCRIPTOR_MISMATCH",
            "status": "ERROR",
            "line": line,
            "owner": resolved_owner,
            "method": method,
            "requested_descriptor": desc,
            "resolved_descriptor": best["descriptor"] if best else None,
            "mapping": best,
            "candidates": candidates,
            "reason": (
                "O método existe, mas o descriptor usado pelo "
                "Mixin não corresponde ao mapping."
            ),
        }

    # ------------------------------------------------------------------------

    def resolve_mixin_class(self, jf, line, target):

        target = target.strip()

        resolved = self.resolve_java_type(
            target,
            jf
        )

        # normalização de inner classes
        mapped = self.db.resolve_class(
            resolved
        )

        if mapped:

            if mapped != resolved:

                return {
                    "status": "FIX",
                    "type": "INNER_CLASS",
                    "line": line,
                    "requested": target,
                    "resolved": mapped,
                    "reason": (
                        "Target corresponde a uma classe/inner class "
                        "existente no mapping."
                    ),
                }

            return {
                "status": "OK",
                "type": "MIXIN_TARGET",
                "line": line,
                "requested": target,
                "resolved": mapped,
            }

        # JDK não precisa existir no client.txt
        if self.is_external(resolved):

            return {
                "status": "EXTERNAL",
                "type": "JDK_TARGET",
                "line": line,
                "requested": target,
                "resolved": resolved,
            }

        return {
            "status": "ERROR",
            "type": "MIXIN_TARGET",
            "line": line,
            "requested": target,
            "reason": (
                "Classe alvo não encontrada."
            ),
        }

    # ------------------------------------------------------------------------

    def scan_file(self, jf):

        text = jf.text

        # ------------------------------------------------------------
        # @Mixin
        # ------------------------------------------------------------

        mixin_pattern = re.compile(
            r"@Mixin\s*\((.*?)\)",
            re.DOTALL
        )

        for m in mixin_pattern.finditer(text):

            body = m.group(1)

            line = line_number_from_source(
                text,
                m.start()
            )

            targets = re.findall(
                r"([A-Za-z_][\w.$]*)\.class",
                body
            )

            for target in targets:

                result = self.resolve_mixin_class(
                    jf,
                    line,
                    target
                )

                result["file"] = jf.path

                self.results.append(result)

        # ------------------------------------------------------------
        # @At
        # ------------------------------------------------------------

        for line, at_text in self.parse_at(text):

            value = self.parse_annotation_value(
                at_text,
                "value"
            )

            if not value:
                self.results.append({
                    "status": "WARNING",
                    "type": "AT_VALUE",
                    "file": jf.path,
                    "line": line,
                    "reason": (
                        "@At não possui value detectável."
                    ),
                })

                continue

            if value == "INVOKE":

                result = self.resolve_invoke(
                    jf,
                    line,
                    at_text
                )

                if result:
                    result["file"] = jf.path
                    self.results.append(result)

            elif value == "FIELD":

                target = self.parse_annotation_value(
                    at_text,
                    "target"
                )

                if target:

                    self.resolve_field(
                        jf,
                        line,
                        target
                    )

        # ------------------------------------------------------------
        # @Shadow
        # ------------------------------------------------------------

        shadow_pattern = re.compile(
            r"@Shadow(?:\s*\([^)]*\))?\s+"
            r"(?:private\s+|protected\s+|public\s+)?"
            r"(?:static\s+)?"
            r"[\w.$<>\[\]]+\s+"
            r"(\w+)\s*;"
        )

        for m in shadow_pattern.finditer(text):

            line = line_number_from_source(
                text,
                m.start()
            )

            name = m.group(1)

            self.results.append({
                "status": "INFO",
                "type": "SHADOW",
                "file": jf.path,
                "line": line,
                "symbol": name,
                "reason": (
                    "@Shadow detectado. O owner depende do @Mixin "
                    "target e deve ser validado contra campos."
                ),
            })

    # ------------------------------------------------------------------------

    def resolve_field(self, jf, line, target):

        m = re.match(
            r"^L([^;]+);([^:]+):(.+)$",
            target or ""
        )

        if not m:
            self.results.append({
                "status": "WARNING",
                "type": "FIELD_TARGET",
                "file": jf.path,
                "line": line,
                "target": target,
                "reason": (
                    "Não foi possível interpretar o target FIELD."
                ),
            })
            return

        owner = m.group(1).replace("/", ".")
        field = m.group(2)
        desc = m.group(3)

        if self.is_external(owner):

            self.results.append({
                "status": "EXTERNAL",
                "type": "FIELD_EXTERNAL",
                "file": jf.path,
                "line": line,
                "owner": owner,
                "field": field,
                "descriptor": desc,
                "reason": (
                    "Campo pertence a uma classe externa/JDK."
                ),
            })

            return

        resolved_owner = self.db.resolve_class(owner)

        if not resolved_owner:

            self.results.append({
                "status": "ERROR",
                "type": "FIELD_OWNER",
                "file": jf.path,
                "line": line,
                "owner": owner,
                "field": field,
                "descriptor": desc,
                "reason": (
                    "Owner do FIELD não encontrado."
                ),
            })

            return

        candidates = self.db.find_field(
            resolved_owner,
            field
        )

        if candidates:

            self.results.append({
                "status": "OK",
                "type": "FIELD",
                "file": jf.path,
                "line": line,
                "owner": resolved_owner,
                "field": field,
                "descriptor": desc,
                "mapping": candidates,
            })

        else:

            self.results.append({
                "status": "ERROR",
                "type": "FIELD_NOT_FOUND",
                "file": jf.path,
                "line": line,
                "owner": resolved_owner,
                "field": field,
                "descriptor": desc,
                "reason": (
                    "Campo não encontrado no owner."
                ),
            })

    # ------------------------------------------------------------------------

    def run(self):

        for index, jf in enumerate(
            self.java_files,
            1
        ):

            print(
                f"\r[RESOLVE] "
                f"{index}/{len(self.java_files)} "
                f"{jf.path}",
                end="",
                flush=True
            )

            self.scan_file(jf)

        print()

        return self.results


# ============================================================================
# RELATÓRIO
# ============================================================================

def print_result(result):

    status = result.get("status", "?")
    rtype = result.get("type", "?")

    if status == "OK":
        prefix = "[OK]"

    elif status == "FIX":
        prefix = "[FIX]"

    elif status == "EXTERNAL":
        prefix = "[EXTERNAL]"

    elif status == "ERROR":
        prefix = "[ERROR]"

    elif status == "WARNING":
        prefix = "[WARNING]"

    else:
        prefix = "[INFO]"

    print()
    print(
        f"{prefix} "
        f"{result.get('file', '')}:"
        f"{result.get('line', '?')}"
    )

    print(
        f"  Categoria : {rtype}"
    )

    if result.get("owner"):
        print(
            f"  Owner     : {result['owner']}"
        )

    if result.get("method"):
        print(
            f"  Método    : {result['method']}"
        )

    if result.get("field"):
        print(
            f"  Campo     : {result['field']}"
        )

    if result.get("requested"):
        print(
            f"  Atual     : {result['requested']}"
        )

    if result.get("resolved"):
        print(
            f"  Solução   : {result['resolved']}"
        )

    if result.get("requested_descriptor"):
        print(
            f"  Descriptor atual : "
            f"{result['requested_descriptor']}"
        )

    if result.get("resolved_descriptor"):
        print(
            f"  Descriptor mapping: "
            f"{result['resolved_descriptor']}"
        )

    if result.get("mapping"):

        mapping = result["mapping"]

        if isinstance(mapping, dict):

            print(
                f"  Mapping   : "
                f"{mapping.get('return_type', '')} "
                f"{mapping.get('name', '')}"
                f"({','.join(mapping.get('parameters', []))})"
                f" -> "
                f"{mapping.get('obfuscated', '')}"
            )

    if result.get("reason"):
        print(
            f"  Diagnóstico: "
            f"{result['reason']}"
        )

    if result.get("candidates"):

        print(
            "  Candidatos:"
        )

        for candidate in result["candidates"]:

            print(
                "    - "
                f"{candidate['name']}"
                f"({','.join(candidate['parameters'])})"
                f" -> "
                f"{candidate['descriptor']}"
            )


# ============================================================================
# PATCH ENGINE
# ============================================================================

def generate_patch(results, project):

    patches = []

    for result in results:

        if result.get("status") not in {
            "FIX"
        }:
            continue

        path = result.get("file")

        if not path:
            continue

        try:
            with open(
                path,
                "r",
                encoding="utf-8",
                errors="ignore"
            ) as f:
                original = f.readlines()

        except Exception:
            continue

        line = result.get("line")

        if not line or line > len(original):
            continue

        old = original[line - 1]

        requested = result.get("requested")
        resolved = result.get("resolved")

        if requested and resolved:

            new = old.replace(
                requested + ".class",
                resolved + ".class"
            )

            if new != old:

                diff = difflib.unified_diff(
                    original,
                    original,
                    fromfile=path,
                    tofile=path
                )

                # gerar apenas bloco manual posteriormente
                patches.append(
                    {
                        "file": path,
                        "line": line,
                        "old": old.rstrip("\n"),
                        "new": new.rstrip("\n"),
                    }
                )

    return patches


# ============================================================================
# MAIN
# ============================================================================

def main():

    parser = argparse.ArgumentParser(
        description=(
            "Glacier Mapping Resolver: "
            "resolve erros do mapping_auditor.py"
        )
    )

    parser.add_argument(
        "--project",
        default="."
    )

    parser.add_argument(
        "--mapping",
        required=True
    )

    parser.add_argument(
        "--report",
        required=True
    )

    parser.add_argument(
        "--json",
        default="glacier_resolver_report.json"
    )

    parser.add_argument(
        "--patch",
        default=None
    )

    args = parser.parse_args()

    print()
    print("=" * 78)
    print(
        "              GLACIER MAPPING RESOLVER v3"
    )
    print("=" * 78)

    # ------------------------------------------------------------------
    # REPORT
    # ------------------------------------------------------------------

    print()
    print("[1/4] Carregando relatório do auditor...")

    try:

        with open(
            args.report,
            "r",
            encoding="utf-8"
        ) as f:
            auditor_report = json.load(f)

    except Exception as e:

        print(
            f"[ERRO] Não foi possível ler report: {e}"
        )

        sys.exit(1)

    # ------------------------------------------------------------------
    # MAPPING
    # ------------------------------------------------------------------

    print(
        "[2/4] Carregando client.txt..."
    )

    if not os.path.exists(args.mapping):

        print(
            f"[ERRO] Mapping não encontrado: "
            f"{args.mapping}"
        )

        sys.exit(1)

    db = parse_proguard(
        args.mapping
    )

    print(
        f"      Classes : {db.class_count:,}"
    )

    print(
        f"      Fields  : {db.field_count:,}"
    )

    print(
        f"      Methods : {db.method_count:,}"
    )

    # ------------------------------------------------------------------
    # JAVA
    # ------------------------------------------------------------------

    print(
        "[3/4] Analisando Java/Mixins..."
    )

    resolver = Resolver(
        args.project,
        db
    )

    results = resolver.run()

    # ------------------------------------------------------------------
    # REPORT
    # ------------------------------------------------------------------

    print(
        "[4/4] Gerando diagnóstico..."
    )

    errors = [
        r for r in results
        if r.get("status") == "ERROR"
    ]

    fixes = [
        r for r in results
        if r.get("status") == "FIX"
    ]

    external = [
        r for r in results
        if r.get("status") == "EXTERNAL"
    ]

    warnings = [
        r for r in results
        if r.get("status") == "WARNING"
    ]

    print()
    print("=" * 78)
    print(
        "              RESOLUÇÕES ENCONTRADAS"
    )
    print("=" * 78)

    print(
        f"FIX       : {len(fixes)}"
    )

    print(
        f"EXTERNAL  : {len(external)}"
    )

    print(
        f"ERROR     : {len(errors)}"
    )

    print(
        f"WARNING   : {len(warnings)}"
    )

    # ------------------------------------------------------------------
    # RESULTADOS
    # ------------------------------------------------------------------

    for result in results:

        if result.get("status") in {
            "ERROR",
            "FIX",
            "EXTERNAL"
        }:
            print_result(result)

    # ------------------------------------------------------------------
    # PATCH
    # ------------------------------------------------------------------

    patch_entries = []

    if args.patch:

        patch_entries = generate_patch(
            results,
            args.project
        )

        with open(
            args.patch,
            "w",
            encoding="utf-8"
        ) as f:

            f.write(
                "# GLACIER MAPPING RESOLVER PATCH\n"
            )
            f.write(
                "# Este arquivo NÃO foi aplicado automaticamente.\n\n"
            )

            for entry in patch_entries:

                f.write(
                    f"--- {entry['file']}\n"
                )
                f.write(
                    f"+++ {entry['file']}\n"
                )

                f.write(
                    f"@@ line {entry['line']} @@\n"
                )

                f.write(
                    f"-{entry['old']}\n"
                )

                f.write(
                    f"+{entry['new']}\n\n"
                )

        print()
        print(
            f"Patch: {args.patch}"
        )

    # ------------------------------------------------------------------
    # JSON
    # ------------------------------------------------------------------

    output = {
        "tool": "Glacier Mapping Resolver",
        "version": "3",
        "project": os.path.abspath(
            args.project
        ),
        "mapping": os.path.abspath(
            args.mapping
        ),
        "auditor_report": os.path.abspath(
            args.report
        ),
        "mapping_stats": {
            "classes": db.class_count,
            "fields": db.field_count,
            "methods": db.method_count,
        },
        "results": results,
        "summary": {
            "fix": len(fixes),
            "external": len(external),
            "error": len(errors),
            "warning": len(warnings),
        },
    }

    with open(
        args.json,
        "w",
        encoding="utf-8"
    ) as f:

        json.dump(
            output,
            f,
            indent=2,
            ensure_ascii=False
        )

    # ------------------------------------------------------------------
    # FINAL
    # ------------------------------------------------------------------

    print()
    print("=" * 78)

    if errors:
        print(
            "RESULTADO: AINDA EXISTEM INCOMPATIBILIDADES"
        )

    elif fixes:
        print(
            "RESULTADO: SOLUÇÕES ENCONTRADAS"
        )

    else:
        print(
            "RESULTADO: NENHUM PROBLEMA CRÍTICO"
        )

    print("=" * 78)

    print(
        f"JSON: {args.json}"
    )

    if args.patch:
        print(
            f"PATCH: {args.patch}"
        )


if __name__ == "__main__":
    main()
