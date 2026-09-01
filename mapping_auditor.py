#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
======================================================================
GLACIER MAPPING AUDITOR v2
======================================================================

Auditor estático para:

    Minecraft Java / Fabric / Sponge Mixin
    + ProGuard client.txt
    + código Java

Compatível com o formato:

    com.example.Class -> abc:

    java.lang.String field -> a

    139:203:void createTexture(
        java.lang.String,int,...
    ) -> createTexture

Principais recursos:

    * Parser ProGuard robusto
    * Classes
    * Inner classes
    * Fields
    * Methods
    * Descriptors
    * @Mixin
    * @Inject
    * @Redirect
    * @ModifyArg
    * @ModifyArgs
    * @ModifyVariable
    * @ModifyConstant
    * @Overwrite
    * @Shadow
    * @Accessor
    * @Invoker
    * @At
    * FIELD target
    * INVOKE target
    * NEW target
    * RETURN
    * Owner resolution
    * Inner-class resolution
    * JVM descriptor validation
    * Imports
    * Minecraft imports
    * JSON report
    * ERROR / WARNING / OK

======================================================================
USO
======================================================================

python3 mapping_auditor.py \
    --project . \
    --mapping "/storage/emulated/0/Download/client.txt"

JSON:

python3 mapping_auditor.py \
    --project . \
    --mapping "/storage/emulated/0/Download/client.txt" \
    --json glacier_mapping_report.json

======================================================================
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple


# ======================================================================
# DATA
# ======================================================================

@dataclass
class MappingField:
    owner: str
    type_name: str
    original_name: str
    obfuscated_name: str
    line: int

    def descriptor(self, mappings):
        return mappings.type_to_descriptor(self.type_name)


@dataclass
class MappingMethod:
    owner: str
    return_type: str
    original_name: str
    parameters: List[str]
    obfuscated_name: str
    start_line: Optional[int]
    end_line: Optional[int]
    raw_signature: str
    line: int

    def descriptor(self, mappings):

        params = "".join(
            mappings.type_to_descriptor(x)
            for x in self.parameters
        )

        ret = mappings.type_to_descriptor(
            self.return_type
        )

        return "(" + params + ")" + ret


@dataclass
class MappingClass:

    original_name: str
    obfuscated_name: str
    line: int

    source_file: Optional[str] = None

    fields: List[MappingField] = field(
        default_factory=list
    )

    methods: List[MappingMethod] = field(
        default_factory=list
    )

    fields_by_name: Dict[
        str,
        List[MappingField]
    ] = field(
        default_factory=dict
    )

    methods_by_name: Dict[
        str,
        List[MappingMethod]
    ] = field(
        default_factory=dict
    )

    def add_field(self, item):

        self.fields.append(item)

        self.fields_by_name.setdefault(
            item.original_name,
            []
        ).append(item)

    def add_method(self, item):

        self.methods.append(item)

        self.methods_by_name.setdefault(
            item.original_name,
            []
        ).append(item)


@dataclass
class Finding:

    severity: str
    category: str
    file: str
    line: int
    symbol: str
    message: str

    solution: str = ""

    mapping_context: str = ""

    confidence: str = "high"

    def to_dict(self):

        return {
            "severity": self.severity,
            "category": self.category,
            "file": self.file,
            "line": self.line,
            "symbol": self.symbol,
            "message": self.message,
            "solution": self.solution,
            "mapping_context": self.mapping_context,
            "confidence": self.confidence,
        }


# ======================================================================
# PROGUARD
# ======================================================================

class ProguardMappings:

    CLASS_RE = re.compile(
        r"^([^\s]+)\s+->\s+([^\s:]+):\s*$"
    )

    METHOD_RE = re.compile(
        r"""
        ^\s*
        (?:(\d+):(\d+):)?
        (.+?)
        \s+
        ([^\s(]+)
        \((.*?)\)
        \s*->\s*
        ([^\s]+)
        \s*$
        """,
        re.VERBOSE
    )

    FIELD_RE = re.compile(
        r"^\s+(.+?)\s+([^\s]+)\s+->\s+([^\s]+)\s*$"
    )

    SOURCE_RE = re.compile(
        r'"fileName"\s*:\s*"([^"]+)"'
    )

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

    def __init__(self):

        self.classes = {}

        self.official_to_obf = {}

        self.obf_to_official = {}

        self.total_classes = 0
        self.total_fields = 0
        self.total_methods = 0

    # ------------------------------------------------------------------
    # LOAD
    # ------------------------------------------------------------------

    def load(self, path):

        current = None

        with open(
            path,
            "r",
            encoding="utf-8",
            errors="replace"
        ) as f:

            for number, raw in enumerate(
                f,
                1
            ):

                line = raw.rstrip("\n")

                if not line.strip():
                    continue

                stripped = line.strip()

                # ------------------------------------------------------
                # COMMENTS
                # ------------------------------------------------------

                if stripped.startswith("#"):

                    if current:

                        match = self.SOURCE_RE.search(
                            stripped
                        )

                        if match:

                            current.source_file = (
                                match.group(1)
                            )

                    continue

                # ------------------------------------------------------
                # CLASS
                # ------------------------------------------------------

                match = self.CLASS_RE.match(
                    line
                )

                if match:

                    original = match.group(1)
                    obfuscated = match.group(2)

                    current = MappingClass(
                        original_name=original,
                        obfuscated_name=obfuscated,
                        line=number
                    )

                    self.classes[
                        original
                    ] = current

                    self.official_to_obf[
                        original
                    ] = obfuscated

                    self.obf_to_official[
                        obfuscated
                    ] = original

                    self.total_classes += 1

                    continue

                if current is None:
                    continue

                # ------------------------------------------------------
                # METHOD
                # ------------------------------------------------------

                match = self.METHOD_RE.match(
                    line
                )

                if match:

                    start = match.group(1)
                    end = match.group(2)

                    return_type = (
                        match.group(3).strip()
                    )

                    method_name = (
                        match.group(4).strip()
                    )

                    params_raw = (
                        match.group(5).strip()
                    )

                    obfuscated_name = (
                        match.group(6).strip()
                    )

                    if params_raw:
                        parameters = [
                            x.strip()
                            for x in params_raw.split(",")
                        ]
                    else:
                        parameters = []

                    item = MappingMethod(
                        owner=current.original_name,
                        return_type=return_type,
                        original_name=method_name,
                        parameters=parameters,
                        obfuscated_name=obfuscated_name,
                        start_line=(
                            int(start)
                            if start
                            else None
                        ),
                        end_line=(
                            int(end)
                            if end
                            else None
                        ),
                        raw_signature=line.strip(),
                        line=number
                    )

                    current.add_method(item)

                    self.total_methods += 1

                    continue

                # ------------------------------------------------------
                # FIELD
                # ------------------------------------------------------

                match = self.FIELD_RE.match(
                    line
                )

                if match:

                    type_name = (
                        match.group(1).strip()
                    )

                    original_name = (
                        match.group(2).strip()
                    )

                    obfuscated_name = (
                        match.group(3).strip()
                    )

                    item = MappingField(
                        owner=current.original_name,
                        type_name=type_name,
                        original_name=original_name,
                        obfuscated_name=obfuscated_name,
                        line=number
                    )

                    current.add_field(item)

                    self.total_fields += 1

    # ------------------------------------------------------------------
    # CLASS RESOLUTION
    # ------------------------------------------------------------------

    def get_class(self, name):

        if not name:
            return None

        name = self.normalize_class_name(
            name
        )

        if name in self.classes:

            return self.classes[name]

        # Try progressively converting the final
        # dot-separated inner classes into '$'.

        parts = name.split(".")

        for i in range(
            len(parts) - 1,
            0,
            -1
        ):

            outer = ".".join(
                parts[:i]
            )

            inner = "$".join(
                parts[i:]
            )

            candidate = (
                outer + "$" + inner
            )

            if candidate in self.classes:

                return self.classes[
                    candidate
                ]

        return None

    def resolve_class_name(
        self,
        name
    ):

        if not name:
            return None

        name = self.normalize_class_name(
            name
        )

        cls = self.get_class(name)

        if cls:
            return cls.original_name

        # Obfuscated -> official

        if name in self.obf_to_official:

            return self.obf_to_official[
                name
            ]

        # Official

        if name in self.classes:

            return name

        # Inner class fallback

        parts = name.split(".")

        for i in range(
            len(parts) - 1,
            0,
            -1
        ):

            candidate = (
                ".".join(parts[:i])
                + "$"
                + "$".join(parts[i:])
            )

            if candidate in self.classes:

                return candidate

        return None

    @staticmethod
    def normalize_class_name(
        name
    ):

        name = name.strip()

        name = name.replace(
            "/",
            "."
        )

        name = re.sub(
            r"^L|;$",
            "",
            name
        )

        return name

    # ------------------------------------------------------------------
    # METHODS / FIELDS
    # ------------------------------------------------------------------

    def find_methods(
        self,
        owner,
        name
    ):

        cls = self.get_class(
            owner
        )

        if not cls:
            return []

        return cls.methods_by_name.get(
            name,
            []
        )

    def find_fields(
        self,
        owner,
        name
    ):

        cls = self.get_class(
            owner
        )

        if not cls:
            return []

        return cls.fields_by_name.get(
            name,
            []
        )

    # ------------------------------------------------------------------
    # TYPE
    # ------------------------------------------------------------------

    def type_to_descriptor(
        self,
        type_name
    ):

        type_name = type_name.strip()

        if type_name in self.PRIMITIVES:

            return self.PRIMITIVES[
                type_name
            ]

        if type_name.endswith("[]"):

            return (
                "["
                + self.type_to_descriptor(
                    type_name[:-2]
                )
            )

        # Generic stripping

        type_name = re.sub(
            r"<.*>",
            "",
            type_name
        )

        # Resolve official class to mapping
        # descriptor is still represented by
        # the official owner when comparing
        # source-level mapping information.

        clean = type_name.replace(
            ".",
            "/"
        )

        return (
            "L"
            + clean
            + ";"
        )

    # ------------------------------------------------------------------
    # STATS
    # ------------------------------------------------------------------

    def stats(self):

        return {
            "classes": self.total_classes,
            "fields": self.total_fields,
            "methods": self.total_methods
        }


# ======================================================================
# JAVA ANALYZER
# ======================================================================

class JavaAnalyzer:

    def __init__(
        self,
        project_root,
        mappings
    ):

        self.project_root = Path(
            project_root
        ).resolve()

        self.mappings = mappings

        self.java_files = []

        self.findings = []

        self.stats = {
            "java_files": 0,
            "mixins": 0,
            "injects": 0,
            "redirects": 0,
            "modify_args": 0,
            "modify_variables": 0,
            "modify_constants": 0,
            "overwrites": 0,
            "shadows": 0,
            "accessors": 0,
            "invokers": 0,
            "at": 0,
            "at_fields": 0,
            "at_methods": 0,
            "at_new": 0,
        }

    # ------------------------------------------------------------------
    # FILES
    # ------------------------------------------------------------------

    def discover_files(self):

        ignored = {
            ".git",
            ".gradle",
            "build",
            "out",
            "target",
            "node_modules",
        }

        for root, dirs, files in os.walk(
            self.project_root
        ):

            dirs[:] = [
                d
                for d in dirs
                if d not in ignored
            ]

            for filename in files:

                if filename.endswith(
                    ".java"
                ):

                    self.java_files.append(
                        Path(root)
                        / filename
                    )

        self.stats[
            "java_files"
        ] = len(
            self.java_files
        )

    # ------------------------------------------------------------------
    # LINE
    # ------------------------------------------------------------------

    @staticmethod
    def line_number(
        content,
        position
    ):

        return (
            content.count(
                "\n",
                0,
                position
            )
            + 1
        )

    # ------------------------------------------------------------------
    # RELATIVE
    # ------------------------------------------------------------------

    def relative(
        self,
        path
    ):

        try:

            return str(
                path.relative_to(
                    self.project_root
                )
            )

        except ValueError:

            return str(path)

    # ------------------------------------------------------------------
    # FINDING
    # ------------------------------------------------------------------

    def add(
        self,
        severity,
        category,
        path,
        line,
        symbol,
        message,
        solution="",
        mapping_context="",
        confidence="high"
    ):

        self.findings.append(
            Finding(
                severity=severity,
                category=category,
                file=self.relative(path),
                line=line,
                symbol=symbol,
                message=message,
                solution=solution,
                mapping_context=mapping_context,
                confidence=confidence
            )
        )

    # ------------------------------------------------------------------
    # IMPORTS
    # ------------------------------------------------------------------

    @staticmethod
    def parse_imports(
        content
    ):

        result = {}

        pattern = re.compile(
            r"\bimport\s+"
            r"(?:static\s+)?"
            r"([A-Za-z0-9_.$]+)\s*;"
        )

        for match in pattern.finditer(
            content
        ):

            full = match.group(1)

            simple = full.split(
                "."
            )[-1]

            result[
                simple
            ] = full

        return result

    # ------------------------------------------------------------------
    # PACKAGE
    # ------------------------------------------------------------------

    @staticmethod
    def parse_package(
        content
    ):

        match = re.search(
            r"\bpackage\s+"
            r"([A-Za-z0-9_.]+)\s*;",
            content
        )

        return (
            match.group(1)
            if match
            else ""
        )

    # ------------------------------------------------------------------
    # TYPE
    # ------------------------------------------------------------------

    def resolve_type(
        self,
        name,
        imports,
        package
    ):

        name = name.strip()

        name = name.replace(
            "$",
            "$"
        )

        name = name.replace(
            "[]",
            ""
        )

        # Imported

        if name in imports:

            return imports[
                name
            ]

        # Direct official

        if name in self.mappings.classes:

            return name

        # Same package

        if package:

            candidate = (
                package
                + "."
                + name
            )

            if self.mappings.get_class(
                candidate
            ):

                return (
                    self.mappings
                    .get_class(candidate)
                    .original_name
                )

        # Already dotted

        if self.mappings.get_class(
            name
        ):

            return (
                self.mappings
                .get_class(name)
                .original_name
            )

        # Inner class:
        #
        # SectionRenderDispatcher.RenderSection
        #
        # ->
        #
        # SectionRenderDispatcher$RenderSection

        normalized = (
            name.replace(
                "/",
                "."
            )
        )

        parts = normalized.split(
            "."
        )

        for i in range(
            len(parts) - 1,
            0,
            -1
        ):

            candidate = (
                ".".join(parts[:i])
                + "$"
                + "$".join(parts[i:])
            )

            cls = (
                self.mappings.get_class(
                    candidate
                )
            )

            if cls:

                return cls.original_name

        # Unique suffix

        candidates = []

        suffix = "." + name

        for class_name in (
            self.mappings.classes
        ):

            if (
                class_name.endswith(
                    suffix
                )
                or class_name == name
            ):

                candidates.append(
                    class_name
                )

        if len(candidates) == 1:

            return candidates[0]

        return None

    # ------------------------------------------------------------------
    # ANNOTATION BLOCKS
    # ------------------------------------------------------------------

    @staticmethod
    def annotation_blocks(
        content,
        annotation
    ):

        pattern = re.compile(
            r"@" + re.escape(
                annotation
            ) + r"\s*\("
        )

        results = []

        for match in pattern.finditer(
            content
        ):

            open_pos = content.find(
                "(",
                match.start()
            )

            if open_pos < 0:
                continue

            depth = 0
            close_pos = None

            for i in range(
                open_pos,
                len(content)
            ):

                char = content[i]

                if char == "(":

                    depth += 1

                elif char == ")":

                    depth -= 1

                    if depth == 0:

                        close_pos = i
                        break

            if close_pos is None:
                continue

            results.append(
                (
                    match.start(),
                    content[
                        open_pos + 1:
                        close_pos
                    ]
                )
            )

        return results

    # ------------------------------------------------------------------
    # @MIXIN
    # ------------------------------------------------------------------

    def audit_mixins(
        self,
        path,
        content,
        imports,
        package
    ):

        for position, body in (
            self.annotation_blocks(
                content,
                "Mixin"
            )
        ):

            self.stats[
                "mixins"
            ] += 1

            line = self.line_number(
                content,
                position
            )

            targets = []

            # value = Foo.class

            targets += re.findall(
                r"\bvalue\s*=\s*"
                r"([A-Za-z0-9_.$]+)"
                r"\s*\.class",
                body
            )

            # targets = { Foo.class, Bar.class }

            targets += re.findall(
                r"(?<![=\w])"
                r"([A-Za-z0-9_.$]+)"
                r"\s*\.class",
                body
            )

            # Remove duplicates

            targets = list(
                dict.fromkeys(
                    targets
                )
            )

            if not targets:

                self.add(
                    "WARNING",
                    "MIXIN_TARGET",
                    path,
                    line,
                    "@Mixin",
                    "Nenhum target de classe foi resolvido.",
                    "Verifique o @Mixin.",
                    confidence="medium"
                )

                continue

            for target in targets:

                resolved = (
                    self.resolve_type(
                        target,
                        imports,
                        package
                    )
                )

                if not resolved:

                    self.add(
                        "ERROR",
                        "MIXIN_TARGET",
                        path,
                        line,
                        target,
                        (
                            f"Classe alvo '{target}' "
                            "não foi encontrada no "
                            "client.txt."
                        ),
                        (
                            "Verifique o nome oficial, "
                            "inner class e versão "
                            "do mapping."
                        )
                    )

                    continue

                mapping = (
                    self.mappings.get_class(
                        resolved
                    )
                )

                self.add(
                    "OK",
                    "MIXIN_TARGET",
                    path,
                    line,
                    target,
                    (
                        f"Target resolvido: "
                        f"{mapping.original_name}"
                    ),
                    (
                        f"Obfuscated: "
                        f"{mapping.obfuscated_name}"
                    ),
                    (
                        f"{mapping.original_name}"
                        f" -> "
                        f"{mapping.obfuscated_name}"
                    )
                )

    # ------------------------------------------------------------------
    # FIND NEAREST MIXIN
    # ------------------------------------------------------------------

    def nearest_mixin(
        self,
        content,
        position,
        imports,
        package
    ):

        prefix = content[
            :position
        ]

        blocks = self.annotation_blocks(
            prefix,
            "Mixin"
        )

        if not blocks:
            return None

        _, body = blocks[-1]

        targets = re.findall(
            r"([A-Za-z0-9_.$]+)"
            r"\s*\.class",
            body
        )

        for target in targets:

            resolved = self.resolve_type(
                target,
                imports,
                package
            )

            if resolved:
                return resolved

        return None

    # ------------------------------------------------------------------
    # METHOD SPEC
    # ------------------------------------------------------------------

    @staticmethod
    def extract_method_specs(
        body
    ):

        result = []

        # method = "foo"

        match = re.search(
            r"\bmethod\s*=\s*"
            r'"([^"]+)"',
            body,
            re.DOTALL
        )

        if match:

            result.append(
                match.group(1)
            )

        # method = {"foo", "bar"}

        array = re.search(
            r"\bmethod\s*=\s*"
            r"\{(.*?)\}",
            body,
            re.DOTALL
        )

        if array:

            result.extend(
                re.findall(
                    r'"([^"]+)"',
                    array.group(1)
                )
            )

        return list(
            dict.fromkeys(
                result
            )
        )

    # ------------------------------------------------------------------
    # INJECTIONS
    # ------------------------------------------------------------------

    def audit_injections(
        self,
        path,
        content,
        imports,
        package
    ):

        annotations = {
            "Inject": "injects",
            "Redirect": "redirects",
            "ModifyArg": "modify_args",
            "ModifyArgs": "modify_args",
            "ModifyVariable": "modify_variables",
            "ModifyConstant": "modify_constants",
        }

        for annotation, stat_key in (
            annotations.items()
        ):

            for position, body in (
                self.annotation_blocks(
                    content,
                    annotation
                )
            ):

                self.stats[
                    stat_key
                ] += 1

                line = self.line_number(
                    content,
                    position
                )

                methods = (
                    self.extract_method_specs(
                        body
                    )
                )

                if not methods:

                    self.add(
                        "WARNING",
                        "METHOD_TARGET",
                        path,
                        line,
                        "method",
                        (
                            f"@{annotation} "
                            "não possui method= "
                            "detectável."
                        ),
                        "Verifique a anotação.",
                        confidence="medium"
                    )

                for method_spec in methods:

                    self.audit_method(
                        path,
                        content,
                        imports,
                        package,
                        position,
                        line,
                        method_spec,
                        annotation
                    )

                self.audit_at(
                    path,
                    content,
                    position,
                    body
                )

        # --------------------------------------------------------------
        # OVERWRITE
        # --------------------------------------------------------------

        for position, body in (
            self.annotation_blocks(
                content,
                "Overwrite"
            )
        ):

            self.stats[
                "overwrites"
            ] += 1

            line = self.line_number(
                content,
                position
            )

            owner = self.nearest_mixin(
                content,
                position,
                imports,
                package
            )

            method_name = (
                self.find_declared_method(
                    content,
                    position
                )
            )

            if owner and method_name:

                self.validate_method_name(
                    path,
                    line,
                    owner,
                    method_name,
                    "Overwrite"
                )

    # ------------------------------------------------------------------
    # METHOD
    # ------------------------------------------------------------------

    def audit_method(
        self,
        path,
        content,
        imports,
        package,
        position,
        line,
        method_spec,
        annotation
    ):

        owner = self.nearest_mixin(
            content,
            position,
            imports,
            package
        )

        if not owner:

            self.add(
                "WARNING",
                "METHOD_OWNER",
                path,
                line,
                method_spec,
                (
                    f"Owner de @{annotation} "
                    "não pôde ser resolvido."
                ),
                (
                    "Confirme o target de @Mixin."
                ),
                confidence="medium"
            )

            return

        method_name = method_spec

        descriptor = None

        if "(" in method_spec:

            method_name = method_spec[
                :method_spec.find("(")
            ]

            descriptor = method_spec[
                method_spec.find("("):
            ]

        candidates = (
            self.mappings.find_methods(
                owner,
                method_name
            )
        )

        if not candidates:

            self.add(
                "ERROR",
                "METHOD_TARGET",
                path,
                line,
                method_spec,
                (
                    f"Método '{method_name}' "
                    f"não existe em {owner}."
                ),
                (
                    "Verifique o nome e o mapping "
                    "da versão atual."
                )
            )

            return

        # Descriptor validation

        if descriptor:

            exact = []

            for candidate in candidates:

                candidate_descriptor = (
                    candidate.descriptor(
                        self.mappings
                    )
                )

                if (
                    candidate_descriptor
                    == descriptor
                ):

                    exact.append(
                        candidate
                    )

            if not exact:

                self.add(
                    "ERROR",
                    "METHOD_DESCRIPTOR",
                    path,
                    line,
                    method_spec,
                    (
                        f"O método existe em "
                        f"{owner}, porém nenhum "
                        "descriptor corresponde."
                    ),
                    (
                        "Confira parâmetros e "
                        "tipo de retorno."
                    ),
                    "\n".join(
                        (
                            x.raw_signature
                            + " | descriptor="
                            + x.descriptor(
                                self.mappings
                            )
                        )
                        for x in candidates
                    )
                )

                return

            candidates = exact

        self.add(
            "OK",
            "METHOD_TARGET",
            path,
            line,
            method_spec,
            (
                f"@{annotation}: método "
                f"'{method_name}' encontrado "
                f"em {owner}."
            ),
            "\n".join(
                (
                    "obfuscated="
                    + x.obfuscated_name
                    + " descriptor="
                    + x.descriptor(
                        self.mappings
                    )
                )
                for x in candidates
            ),
            "\n".join(
                x.raw_signature
                for x in candidates
            )
        )

    # ------------------------------------------------------------------
    # VALIDATE METHOD
    # ------------------------------------------------------------------

    def validate_method_name(
        self,
        path,
        line,
        owner,
        method_name,
        annotation
    ):

        candidates = (
            self.mappings.find_methods(
                owner,
                method_name
            )
        )

        if not candidates:

            self.add(
                "ERROR",
                "METHOD_TARGET",
                path,
                line,
                method_name,
                (
                    f"Método '{method_name}' "
                    f"não existe em {owner}."
                ),
                (
                    "Atualize o nome conforme "
                    "o client.txt."
                )
            )

            return

        self.add(
            "OK",
            "METHOD_TARGET",
            path,
            line,
            method_name,
            (
                f"@{annotation}: método "
                f"'{method_name}' validado."
            ),
            "\n".join(
                x.raw_signature
                for x in candidates
            )
        )

    # ------------------------------------------------------------------
    # @AT
    # ------------------------------------------------------------------

    def audit_at(
        self,
        path,
        content,
        position,
        body
    ):

        # --------------------------------------------------------------
        # We cannot parse @At(...) with a simplistic
        # regex because target strings may contain
        # parentheses.
        # --------------------------------------------------------------

        for at_position, at_body in (
            self.extract_at_blocks(
                body
            )
        ):

            self.stats[
                "at"
            ] += 1

            line = self.line_number(
                content,
                position
            )

            value_match = re.search(
                r"\bvalue\s*=\s*"
                r'"([^"]+)"',
                at_body
            )

            target_match = re.search(
                r"\btarget\s*=\s*"
                r'"([^"]+)"',
                at_body
            )

            value = (
                value_match.group(1)
                if value_match
                else None
            )

            target = (
                target_match.group(1)
                if target_match
                else None
            )

            if not value:

                self.add(
                    "WARNING",
                    "AT_VALUE",
                    path,
                    line,
                    "@At",
                    (
                        "@At não possui "
                        "value detectável."
                    ),
                    "Verifique a anotação.",
                    confidence="medium"
                )

                continue

            # ----------------------------------------------------------
            # FIELD
            # ----------------------------------------------------------

            if value == "FIELD":

                self.stats[
                    "at_fields"
                ] += 1

                if target:

                    self.audit_field_target(
                        path,
                        content,
                        line,
                        target
                    )

                else:

                    self.add(
                        "WARNING",
                        "AT_FIELD",
                        path,
                        line,
                        "@At FIELD",
                        (
                            "FIELD sem target explícito."
                        ),
                        (
                            "Verifique se o target "
                            "é fornecido por outro mecanismo."
                        ),
                        confidence="medium"
                    )

                continue

            # ----------------------------------------------------------
            # INVOKE
            # ----------------------------------------------------------

            if value == "INVOKE":

                self.stats[
                    "at_methods"
                ] += 1

                if target:

                    self.audit_invoke_target(
                        path,
                        content,
                        line,
                        target
                    )

                continue

            # ----------------------------------------------------------
            # NEW
            # ----------------------------------------------------------

            if value == "NEW":

                self.stats[
                    "at_new"
                ] += 1

                if target:

                    self.audit_invoke_target(
                        path,
                        content,
                        line,
                        target
                    )

                continue

            # ----------------------------------------------------------
            # Other valid injection points
            # ----------------------------------------------------------

            valid = {
                "HEAD",
                "TAIL",
                "RETURN",
                "JUMP",
                "STORE",
                "LOAD",
                "CONSTANT",
                "NEW",
                "FIELD",
                "INVOKE",
            }

            if value not in valid:

                self.add(
                    "WARNING",
                    "AT_VALUE",
                    path,
                    line,
                    value,
                    (
                        f"Injection point "
                        f"'{value}' não foi "
                        "reconhecido."
                    ),
                    (
                        "Confirme se o injection "
                        "point pertence à versão "
                        "do Mixin usada pelo projeto."
                    ),
                    confidence="medium"
                )

            else:

                self.add(
                    "OK",
                    "AT_VALUE",
                    path,
                    line,
                    value,
                    (
                        f"@At(\"{value}\") "
                        "reconhecido."
                    )
                )

    # ------------------------------------------------------------------
    # EXTRACT @AT
    # ------------------------------------------------------------------

    @staticmethod
    def extract_at_blocks(
        body
    ):

        results = []

        pattern = re.compile(
            r"@At\s*\("
        )

        for match in pattern.finditer(
            body
        ):

            open_pos = body.find(
                "(",
                match.start()
            )

            depth = 0
            quote = False
            escape = False
            close = None

            for i in range(
                open_pos,
                len(body)
            ):

                char = body[i]

                if escape:

                    escape = False
                    continue

                if char == "\\" and quote:

                    escape = True
                    continue

                if char == '"':

                    quote = not quote
                    continue

                if quote:
                    continue

                if char == "(":

                    depth += 1

                elif char == ")":

                    depth -= 1

                    if depth == 0:

                        close = i
                        break

            if close is not None:

                results.append(
                    (
                        match.start(),
                        body[
                            open_pos + 1:
                            close
                        ]
                    )
                )

        return results

    # ------------------------------------------------------------------
    # FIELD TARGET
    # ------------------------------------------------------------------

    def audit_field_target(
        self,
        path,
        content,
        line,
        target
    ):

        parsed = self.parse_member_target(
            target
        )

        if not parsed:

            self.add(
                "WARNING",
                "AT_FIELD",
                path,
                line,
                target,
                (
                    "Não foi possível decompor "
                    "o FIELD target."
                ),
                (
                    "Use o formato JVM, por exemplo "
                    "Lpkg/Class;field:Z."
                ),
                confidence="medium"
            )

            return

        owner, name, descriptor = parsed

        cls = self.mappings.get_class(
            owner
        )

        if not cls:

            self.add(
                "ERROR",
                "AT_FIELD",
                path,
                line,
                target,
                (
                    f"Owner '{owner}' não "
                    "existe no client.txt."
                ),
                (
                    "Verifique o owner e a versão "
                    "do mapping."
                )
            )

            return

        fields = (
            self.mappings.find_fields(
                owner,
                name
            )
        )

        if not fields:

            self.add(
                "ERROR",
                "AT_FIELD",
                path,
                line,
                target,
                (
                    f"Campo '{name}' não "
                    f"existe em {owner}."
                ),
                (
                    "Verifique o nome do campo "
                    "na versão atual."
                )
            )

            return

        exact = []

        if descriptor:

            for item in fields:

                if (
                    item.descriptor(
                        self.mappings
                    )
                    == descriptor
                ):

                    exact.append(
                        item
                    )

        if descriptor and not exact:

            self.add(
                "ERROR",
                "AT_FIELD_DESCRIPTOR",
                path,
                line,
                target,
                (
                    f"Campo '{name}' existe "
                    f"em {owner}, mas o "
                    f"descriptor '{descriptor}' "
                    "não corresponde."
                ),
                (
                    "Verifique o tipo do campo."
                ),
                "\n".join(
                    (
                        x.type_name
                        + " "
                        + x.original_name
                        + " -> "
                        + x.obfuscated_name
                        + " | descriptor="
                        + x.descriptor(
                            self.mappings
                        )
                    )
                    for x in fields
                )
            )

            return

        selected = (
            exact
            if exact
            else fields
        )

        self.add(
            "OK",
            "AT_FIELD",
            path,
            line,
            target,
            (
                f"FIELD target validado: "
                f"{owner}.{name}"
            ),
            "\n".join(
                (
                    x.type_name
                    + " "
                    + x.original_name
                    + " -> "
                    + x.obfuscated_name
                )
                for x in selected
            ),
            "\n".join(
                (
                    x.raw
                    if hasattr(x, "raw")
                    else (
                        x.type_name
                        + " "
                        + x.original_name
                        + " -> "
                        + x.obfuscated_name
                    )
                )
                for x in selected
            )
        )

    # ------------------------------------------------------------------
    # INVOKE TARGET
    # ------------------------------------------------------------------

    def audit_invoke_target(
        self,
        path,
        content,
        line,
        target
    ):

        parsed = self.parse_member_target(
            target
        )

        if not parsed:

            self.add(
                "WARNING",
                "AT_INVOKE",
                path,
                line,
                target,
                (
                    "Não foi possível decompor "
                    "o INVOKE target."
                ),
                (
                    "Use um descriptor JVM "
                    "completo."
                ),
                confidence="medium"
            )

            return

        owner, name, descriptor = parsed

        cls = self.mappings.get_class(
            owner
        )

        if not cls:

            self.add(
                "ERROR",
                "AT_INVOKE",
                path,
                line,
                target,
                (
                    f"Owner '{owner}' "
                    "não existe."
                ),
                (
                    "Verifique a classe alvo."
                )
            )

            return

        methods = (
            self.mappings.find_methods(
                owner,
                name
            )
        )

        if not methods:

            self.add(
                "ERROR",
                "AT_INVOKE",
                path,
                line,
                target,
                (
                    f"Método '{name}' "
                    f"não existe em {owner}."
                ),
                (
                    "Verifique o método "
                    "mapeado."
                )
            )

            return

        if descriptor:

            exact = []

            for item in methods:

                if (
                    item.descriptor(
                        self.mappings
                    )
                    == descriptor
                ):

                    exact.append(
                        item
                    )

            if not exact:

                self.add(
                    "ERROR",
                    "AT_INVOKE_DESCRIPTOR",
                    path,
                    line,
                    target,
                    (
                        f"'{name}' existe em "
                        f"{owner}, mas nenhum "
                        "descriptor corresponde."
                    ),
                    (
                        "Confira os parâmetros "
                        "e retorno."
                    ),
                    "\n".join(
                        (
                            x.raw_signature
                            + " | descriptor="
                            + x.descriptor(
                                self.mappings
                            )
                        )
                        for x in methods
                    )
                )

                return

            methods = exact

        self.add(
            "OK",
            "AT_INVOKE",
            path,
            line,
            target,
            (
                f"INVOKE target validado: "
                f"{owner}.{name}"
            ),
            "\n".join(
                (
                    "obfuscated="
                    + x.obfuscated_name
                    + " descriptor="
                    + x.descriptor(
                        self.mappings
                    )
                )
                for x in methods
            ),
            "\n".join(
                x.raw_signature
                for x in methods
            )
        )

    # ------------------------------------------------------------------
    # TARGET PARSER
    # ------------------------------------------------------------------

    @staticmethod
    def parse_member_target(
        target
    ):

        target = target.strip()

        # --------------------------------------------------------------
        # JVM:
        #
        # Lnet/minecraft/client/Minecraft;noRender:Z
        #
        # Lpkg/Class;method(I)V
        # --------------------------------------------------------------

        match = re.match(
            r"^L([^;]+);"
            r"([A-Za-z0-9_$<>]+)"
            r"(?::(.+)|(\(.*\).*))$",
            target
        )

        if match:

            owner = (
                match.group(1)
                .replace(
                    "/",
                    "."
                )
            )

            name = match.group(2)

            field_descriptor = (
                match.group(3)
            )

            method_descriptor = (
                match.group(4)
            )

            descriptor = (
                field_descriptor
                if field_descriptor
                else method_descriptor
            )

            return (
                owner,
                name,
                descriptor
            )

        # --------------------------------------------------------------
        # Alternative:
        #
        # pkg.Class.field:I
        # pkg.Class.method(I)V
        # --------------------------------------------------------------

        match = re.match(
            r"^(.+)\.([A-Za-z0-9_$<>]+)"
            r"(?::(.+)|(\(.*\).*))$",
            target
        )

        if match:

            return (
                match.group(1).replace(
                    "/",
                    "."
                ),
                match.group(2),
                (
                    match.group(3)
                    or match.group(4)
                )
            )

        return None

    # ------------------------------------------------------------------
    # SHADOW / ACCESSOR / INVOKER
    # ------------------------------------------------------------------

    def audit_accessors(
        self,
        path,
        content,
        imports,
        package
    ):

        for annotation in (
            "Shadow",
            "Accessor",
            "Invoker"
        ):

            for position, body in (
                self.annotation_blocks(
                    content,
                    annotation
                )
            ):

                stat = {
                    "Shadow": "shadows",
                    "Accessor": "accessors",
                    "Invoker": "invokers",
                }[annotation]

                self.stats[
                    stat
                ] += 1

                line = self.line_number(
                    content,
                    position
                )

                owner = self.nearest_mixin(
                    content,
                    position,
                    imports,
                    package
                )

                if not owner:

                    self.add(
                        "WARNING",
                        annotation,
                        path,
                        line,
                        "owner",
                        (
                            f"Owner de "
                            f"@{annotation} "
                            "não pôde ser resolvido."
                        ),
                        (
                            "Confirme @Mixin."
                        ),
                        confidence="medium"
                    )

                    continue

                declaration = (
                    self.member_after_annotation(
                        content,
                        position
                    )
                )

                # ------------------------------------------------------
                # SHADOW
                # ------------------------------------------------------

                if annotation == "Shadow":

                    member = (
                        self.parse_member_declaration(
                            declaration
                        )
                    )

                    if not member:
                        continue

                    name, is_method = member

                    if is_method:

                        methods = (
                            self.mappings.find_methods(
                                owner,
                                name
                            )
                        )

                        if methods:

                            self.add(
                                "OK",
                                "SHADOW_METHOD",
                                path,
                                line,
                                name,
                                (
                                    f"@Shadow método "
                                    f"'{name}' encontrado "
                                    f"em {owner}."
                                ),
                                "\n".join(
                                    x.raw_signature
                                    for x in methods
                                )
                            )

                        else:

                            self.add(
                                "ERROR",
                                "SHADOW_METHOD",
                                path,
                                line,
                                name,
                                (
                                    f"@Shadow método "
                                    f"'{name}' não existe "
                                    f"em {owner}."
                                ),
                                (
                                    "Atualize o nome/"
                                    "assinatura."
                                )
                            )

                    else:

                        fields = (
                            self.mappings.find_fields(
                                owner,
                                name
                            )
                        )

                        if fields:

                            self.add(
                                "OK",
                                "SHADOW_FIELD",
                                path,
                                line,
                                name,
                                (
                                    f"@Shadow field "
                                    f"'{name}' encontrado "
                                    f"em {owner}."
                                ),
                                "\n".join(
                                    (
                                        x.type_name
                                        + " "
                                        + x.original_name
                                        + " -> "
                                        + x.obfuscated_name
                                    )
                                    for x in fields
                                )
                            )

                        else:

                            self.add(
                                "ERROR",
                                "SHADOW_FIELD",
                                path,
                                line,
                                name,
                                (
                                    f"@Shadow field "
                                    f"'{name}' não existe "
                                    f"em {owner}."
                                ),
                                (
                                    "Atualize o nome "
                                    "do campo."
                                )
                            )

                # ------------------------------------------------------
                # ACCESSOR / INVOKER
                # ------------------------------------------------------

                else:

                    quoted = re.findall(
                        r'"([^"]+)"',
                        body
                    )

                    target = (
                        quoted[0]
                        if quoted
                        else None
                    )

                    if not target:

                        member = (
                            self.parse_member_declaration(
                                declaration
                            )
                        )

                        if member:

                            target = member[0]

                    if not target:
                        continue

                    if annotation == "Accessor":

                        fields = (
                            self.mappings.find_fields(
                                owner,
                                target
                            )
                        )

                        if fields:

                            self.add(
                                "OK",
                                "ACCESSOR",
                                path,
                                line,
                                target,
                                (
                                    f"@Accessor "
                                    f"'{target}' "
                                    f"validado em {owner}."
                                ),
                                "\n".join(
                                    (
                                        x.type_name
                                        + " "
                                        + x.original_name
                                        + " -> "
                                        + x.obfuscated_name
                                    )
                                    for x in fields
                                )
                            )

                        else:

                            self.add(
                                "ERROR",
                                "ACCESSOR",
                                path,
                                line,
                                target,
                                (
                                    f"@Accessor "
                                    f"'{target}' não "
                                    f"foi encontrado "
                                    f"em {owner}."
                                ),
                                (
                                    "Verifique o campo."
                                )
                            )

                    else:

                        methods = (
                            self.mappings.find_methods(
                                owner,
                                target
                            )
                        )

                        if methods:

                            self.add(
                                "OK",
                                "INVOKER",
                                path,
                                line,
                                target,
                                (
                                    f"@Invoker "
                                    f"'{target}' "
                                    f"validado em {owner}."
                                ),
                                "\n".join(
                                    x.raw_signature
                                    for x in methods
                                )
                            )

                        else:

                            self.add(
                                "ERROR",
                                "INVOKER",
                                path,
                                line,
                                target,
                                (
                                    f"@Invoker "
                                    f"'{target}' não "
                                    f"existe em {owner}."
                                ),
                                (
                                    "Verifique nome "
                                    "e assinatura."
                                )
                            )

    # ------------------------------------------------------------------
    # MEMBER DECLARATION
    # ------------------------------------------------------------------

    @staticmethod
    def member_after_annotation(
        content,
        position
    ):

        remainder = content[
            position:
        ]

        close = remainder.find(
            ")"
        )

        if close >= 0:

            return remainder[
                close + 1:
                close + 1500
            ]

        return remainder[
            :1500
        ]

    @staticmethod
    def parse_member_declaration(
        text
    ):

        # Remove subsequent annotations

        text = re.sub(
            r"@\w+(?:\([^)]*\))?",
            " ",
            text
        )

        # Method

        method = re.search(
            r"""
            (?:public|private|protected|
               static|final|abstract|
               synchronized|native|
               volatile|transient|strictfp|
               default|\s)*
            [A-Za-z0-9_.$<>\[\]]+
            \s+
            ([A-Za-z0-9_$]+)
            \s*\(
            """,
            text,
            re.VERBOSE
        )

        if method:

            return (
                method.group(1),
                True
            )

        # Field

        field_match = re.search(
            r"""
            (?:public|private|protected|
               static|final|volatile|
               transient|\s)*
            [A-Za-z0-9_.$<>\[\]]+
            \s+
            ([A-Za-z0-9_$]+)
            \s*(?:=|;)
            """,
            text,
            re.VERBOSE
        )

        if field_match:

            return (
                field_match.group(1),
                False
            )

        return None

    # ------------------------------------------------------------------
    # DECLARED METHOD
    # ------------------------------------------------------------------

    @staticmethod
    def find_declared_method(
        content,
        position
    ):

        remainder = content[
            position:
        ]

        close = remainder.find(
            ")"
        )

        if close >= 0:

            remainder = remainder[
                close + 1:
            ]

        match = re.search(
            r"""
            (?:public|private|protected|
               static|final|abstract|
               synchronized|native|
               strictfp|\s)*
            [A-Za-z0-9_.$<>\[\]]+
            \s+
            ([A-Za-z0-9_$]+)
            \s*\(
            """,
            remainder,
            re.VERBOSE
        )

        if match:

            return match.group(1)

        return None

    # ------------------------------------------------------------------
    # UNKNOWN MINECRAFT IMPORTS
    # ------------------------------------------------------------------

    def audit_imports(
        self,
        path,
        content,
        imports
    ):

        for simple, full in imports.items():

            if not (
                full.startswith(
                    "net.minecraft."
                )
                or full.startswith(
                    "com.mojang.blaze3d."
                )
            ):
                continue

            if self.mappings.get_class(
                full
            ):
                continue

            match = re.search(
                r"\bimport\s+"
                + re.escape(full)
                + r"\s*;",
                content
            )

            line = (
                self.line_number(
                    content,
                    match.start()
                )
                if match
                else 1
            )

            self.add(
                "ERROR",
                "CLASS_MAPPING",
                path,
                line,
                full,
                (
                    f"Classe '{full}' "
                    "não existe no client.txt."
                ),
                (
                    "Verifique se a classe foi "
                    "renomeada/removida ou se "
                    "o mapping pertence à versão correta."
                )
            )

    # ------------------------------------------------------------------
    # FILE
    # ------------------------------------------------------------------

    def analyze_file(
        self,
        path
    ):

        try:

            content = path.read_text(
                encoding="utf-8",
                errors="replace"
            )

        except Exception as exc:

            self.add(
                "ERROR",
                "IO",
                path,
                1,
                str(path),
                f"Erro lendo Java: {exc}"
            )

            return

        imports = self.parse_imports(
            content
        )

        package = self.parse_package(
            content
        )

        self.audit_imports(
            path,
            content,
            imports
        )

        self.audit_mixins(
            path,
            content,
            imports,
            package
        )

        self.audit_injections(
            path,
            content,
            imports,
            package
        )

        self.audit_accessors(
            path,
            content,
            imports,
            package
        )

    # ------------------------------------------------------------------
    # RUN
    # ------------------------------------------------------------------

    def run(self):

        self.discover_files()

        total = len(
            self.java_files
        )

        for index, path in enumerate(
            self.java_files,
            1
        ):

            print(
                "\r[SCAN] "
                f"{index}/{total} "
                f"{self.relative(path)}"
                + " " * 10,
                end="",
                flush=True
            )

            self.analyze_file(
                path
            )

        print()


# ======================================================================
# REPORT
# ======================================================================

def print_report(
    analyzer,
    mappings
):

    counts = {
        "OK": 0,
        "WARNING": 0,
        "ERROR": 0
    }

    for finding in (
        analyzer.findings
    ):

        if finding.severity in counts:

            counts[
                finding.severity
            ] += 1

    print()
    print("=" * 78)
    print(
        "              GLACIER MAPPING AUDITOR v2"
    )
    print("=" * 78)

    print()
    print("MAPPINGS")
    print("-" * 78)

    print(
        f"Classes : {mappings.total_classes:,}"
    )

    print(
        f"Fields  : {mappings.total_fields:,}"
    )

    print(
        f"Methods : {mappings.total_methods:,}"
    )

    print()
    print("JAVA / MIXIN")
    print("-" * 78)

    for key, value in (
        analyzer.stats.items()
    ):

        print(
            f"{key:22} : {value:,}"
        )

    print()
    print("RESULTADO")
    print("-" * 78)

    print(
        f"OK       : {counts['OK']:,}"
    )

    print(
        f"WARNING  : {counts['WARNING']:,}"
    )

    print(
        f"ERROR    : {counts['ERROR']:,}"
    )

    # ------------------------------------------------------------------
    # ERROR REPORT
    # ------------------------------------------------------------------

    errors = [
        x
        for x in analyzer.findings
        if x.severity == "ERROR"
    ]

    if errors:

        print()
        print("=" * 78)
        print("ERROS")
        print("=" * 78)

        for x in errors:

            print()
            print(
                f"[ERROR] {x.file}:{x.line}"
            )

            print(
                f"  Categoria : {x.category}"
            )

            print(
                f"  Símbolo   : {x.symbol}"
            )

            print(
                f"  Problema  : {x.message}"
            )

            if x.solution:

                print(
                    f"  Solução   : {x.solution}"
                )

            if x.mapping_context:

                print(
                    f"  Mapping   : {x.mapping_context}"
                )

            print(
                f"  Confiança : {x.confidence}"
            )

    # ------------------------------------------------------------------
    # WARNINGS
    # ------------------------------------------------------------------

    warnings = [
        x
        for x in analyzer.findings
        if x.severity == "WARNING"
    ]

    if warnings:

        print()
        print("=" * 78)
        print("WARNINGS")
        print("=" * 78)

        for x in warnings:

            print()
            print(
                f"[WARNING] {x.file}:{x.line}"
            )

            print(
                f"  Categoria : {x.category}"
            )

            print(
                f"  Símbolo   : {x.symbol}"
            )

            print(
                f"  Problema  : {x.message}"
            )

            if x.solution:

                print(
                    f"  Sugestão  : {x.solution}"
                )

    # ------------------------------------------------------------------
    # FINAL
    # ------------------------------------------------------------------

    print()
    print("=" * 78)

    if counts["ERROR"]:

        print(
            "RESULTADO FINAL: FAILED"
        )

        print(
            f"{counts['ERROR']} erro(s) "
            "encontrado(s)."
        )

    elif counts["WARNING"]:

        print(
            "RESULTADO FINAL: PASS WITH WARNINGS"
        )

        print(
            "Nenhum erro crítico encontrado."
        )

    else:

        print(
            "RESULTADO FINAL: PASS"
        )

        print(
            "Todos os elementos auditados "
            "foram validados."
        )

    print("=" * 78)


# ======================================================================
# JSON
# ======================================================================

def write_json(
    output,
    analyzer,
    mappings
):

    counts = {
        "OK": 0,
        "WARNING": 0,
        "ERROR": 0
    }

    for x in analyzer.findings:

        if x.severity in counts:

            counts[
                x.severity
            ] += 1

    data = {
        "tool": "Glacier Mapping Auditor",
        "version": "2.0",
        "mapping": mappings.stats(),
        "java": analyzer.stats,
        "summary": counts,
        "findings": [
            x.to_dict()
            for x in analyzer.findings
        ]
    }

    with open(
        output,
        "w",
        encoding="utf-8"
    ) as f:

        json.dump(
            data,
            f,
            indent=2,
            ensure_ascii=False
        )


# ======================================================================
# MAIN
# ======================================================================

def main():

    parser = argparse.ArgumentParser(
        description=(
            "Glacier Mapping Auditor v2"
        )
    )

    parser.add_argument(
        "--project",
        default=".",
        help="Raiz do projeto"
    )

    parser.add_argument(
        "--mapping",
        default=(
            "/storage/emulated/0/Download/"
            "client.txt"
        ),
        help="client.txt"
    )

    parser.add_argument(
        "--json",
        default=(
            "glacier_mapping_report.json"
        ),
        help="Relatório JSON"
    )

    args = parser.parse_args()

    print()
    print("=" * 78)
    print(
        "             GLACIER MAPPING AUDITOR v2"
    )
    print("=" * 78)

    # ------------------------------------------------------------------
    # Validate
    # ------------------------------------------------------------------

    if not os.path.isdir(
        args.project
    ):

        print(
            "[FATAL] Projeto não encontrado:"
        )

        print(
            args.project
        )

        sys.exit(2)

    if not os.path.isfile(
        args.mapping
    ):

        print(
            "[FATAL] Mapping não encontrado:"
        )

        print(
            args.mapping
        )

        sys.exit(2)

    # ------------------------------------------------------------------
    # Mapping
    # ------------------------------------------------------------------

    print()
    print(
        "[1/3] Carregando client.txt..."
    )

    mappings = ProguardMappings()

    try:

        mappings.load(
            args.mapping
        )

    except Exception as exc:

        print(
            "[FATAL] Falha no parser:"
        )

        print(
            str(exc)
        )

        sys.exit(3)

    print(
        f"      Classes : "
        f"{mappings.total_classes:,}"
    )

    print(
        f"      Fields  : "
        f"{mappings.total_fields:,}"
    )

    print(
        f"      Methods : "
        f"{mappings.total_methods:,}"
    )

    # ------------------------------------------------------------------
    # Java
    # ------------------------------------------------------------------

    print()
    print(
        "[2/3] Varredura Java/Mixin..."
    )

    analyzer = JavaAnalyzer(
        args.project,
        mappings
    )

    analyzer.run()

    # ------------------------------------------------------------------
    # Report
    # ------------------------------------------------------------------

    print()
    print(
        "[3/3] Gerando relatório..."
    )

    print_report(
        analyzer,
        mappings
    )

    write_json(
        args.json,
        analyzer,
        mappings
    )

    print()
    print(
        f"JSON: {os.path.abspath(args.json)}"
    )

    # ------------------------------------------------------------------
    # Exit
    # ------------------------------------------------------------------

    errors = sum(
        1
        for x in analyzer.findings
        if x.severity == "ERROR"
    )

    if errors:

        sys.exit(1)

    sys.exit(0)


if __name__ == "__main__":

    main()
