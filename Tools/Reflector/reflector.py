#!/usr/bin/env python3
"""Tucano Reflector — turns TUCANO_TYPE/TUCANO_FIELD annotations into registration code.

Derived in spirit from Esoterica (MIT) — Applications/Reflector. That one is a ~10k-line C++
application with its own project database; this is the pipeline only, which is what the roadmap
(P3-03) asked for: parse the marked headers, emit the registration, and stop.

What it emits is deliberately the *same macro form* the hand-written reflection files already use:

    TUCANO_REFLECT_TYPE_BEGIN(Material)
        TUCANO_PROPERTY(baseColorFactor, Color, .label = "Base color")
    TUCANO_REFLECT_TYPE_END(Material)

Two reasons. The macros are already covered by the gate, so the generator inherits that instead of
inventing a second, untested path to a TypeInfo. And offsets and sizes stay `offsetof`/`sizeof`
rather than numbers baked in by the tool — a generator that hard-codes a layout is wrong the moment
anything changes packing, and wrong silently.

    python reflector.py --out <file.cpp> --manifest <file> [--compile-args <file>] [--check]

--check exits non-zero if the output would differ from what is on disk, without writing. That is
what a build uses to say "the generated file is stale" instead of quietly editing the source tree.

Requires the `libclang` Python package. When it is missing the caller is expected to skip codegen
and use the checked-in output; that is why the generated file is committed rather than treated as
a build artefact.
"""

import argparse
import os
import re
import sys

TYPE_MARK = "tucano.type:"
FIELD_MARK = "tucano.field:"
ENUM_MARK = "tucano.enum:"

# C++ spelling -> CoreType, for the cases where the type says everything. Anything ambiguous about
# *intent* rather than storage (a colour is a vec4) has to be spelled out at the field.
CORE_TYPE_BY_SPELLING = {
    "bool": "Bool",
    "char": "Int32",
    "short": "Int32",
    "int": "Int32",
    "long": "Int32",
    "long long": "Int64",
    "int8_t": "Int32",
    "int16_t": "Int32",
    "int32_t": "Int32",
    "int64_t": "Int64",
    "unsigned char": "UInt32",
    "unsigned short": "UInt32",
    "unsigned int": "UInt32",
    "unsigned long": "UInt32",
    "unsigned long long": "UInt64",
    "uint8_t": "UInt32",
    "uint16_t": "UInt32",
    "uint32_t": "UInt32",
    "uint64_t": "UInt64",
    "size_t": "UInt64",
    "float": "Float",
    "double": "Double",
    "std::string": "String",
    "glm::vec2": "Vec2",
    "glm::vec3": "Vec3",
    "glm::vec4": "Vec4",
    "glm::quat": "Quat",
}

KNOWN_CORE_TYPES = {
    "Bool", "Int32", "UInt32", "Int64", "UInt64", "Float", "Double", "String",
    "Vec2", "Vec3", "Vec4", "Quat", "Color", "Enum", "Struct", "Array",
}


class Field:
    def __init__(self, name, core_type, metadata, struct_type=None):
        self.name = name
        self.core_type = core_type
        self.metadata = metadata
        self.struct_type = struct_type


class ReflectedType:
    def __init__(self, name, header):
        self.name = name
        self.header = header
        self.fields = []


def strip_annotation(text, mark):
    """Annotation payload minus the marker. Stringification leaves the text as written."""
    return text[len(mark):].strip()


def split_core_type(payload):
    """Split `Color, .label = "x"` into ("Color", '.label = "x"').

    Unambiguous because every metadata entry is a designated initialiser and so starts with '.'.
    """
    payload = payload.strip()
    if not payload:
        return None, ""
    head = payload.split(",", 1)
    first = head[0].strip()
    if first and not first.startswith("."):
        if first not in KNOWN_CORE_TYPES:
            raise ValueError(f"'{first}' is not a CoreType")
        return first, (head[1].strip() if len(head) > 1 else "")
    return None, payload


def normalise_spelling(spelling):
    """Drop cv-qualifiers and whitespace noise so the lookup table stays small."""
    text = spelling.strip()
    text = re.sub(r"^const\s+", "", text)
    text = re.sub(r"\s+", " ", text)
    return text


def infer_core_type(field_type, reflected_names):
    """CoreType from the declared C++ type, or None when the type cannot say."""
    import clang.cindex as ci

    canonical = field_type
    # An array is a run of elements; the element type is what decides how a row is drawn.
    if canonical.kind == ci.TypeKind.CONSTANTARRAY:
        element = infer_core_type(canonical.element_type, reflected_names)
        return ("Array", element, canonical.element_count) if element else None

    spelling = normalise_spelling(canonical.spelling)
    if spelling in CORE_TYPE_BY_SPELLING:
        return (CORE_TYPE_BY_SPELLING[spelling], None, 0)

    decl = canonical.get_declaration()
    if decl is not None and decl.kind == ci.CursorKind.ENUM_DECL:
        return ("Enum", None, 0)

    bare = spelling.split("::")[-1]
    if bare in reflected_names:
        return ("Struct", None, 0)

    return None


def collect(cursor, header, out_types, reflected_names):
    import clang.cindex as ci

    for node in cursor.walk_preorder():
        if node.kind not in (ci.CursorKind.STRUCT_DECL, ci.CursorKind.CLASS_DECL):
            continue
        if not node.is_definition():
            continue
        # Only what this header declares. Walking a translation unit reaches every include, and a
        # type must be emitted once, by the header that owns it.
        if node.location.file is None:
            continue
        if os.path.normcase(os.path.abspath(node.location.file.name)) != os.path.normcase(
            os.path.abspath(header)
        ):
            continue

        annotations = [
            c.spelling for c in node.get_children() if c.kind == ci.CursorKind.ANNOTATE_ATTR
        ]
        if not any(a.startswith(TYPE_MARK) for a in annotations):
            continue

        reflected = ReflectedType(node.spelling, header)
        for member in node.get_children():
            if member.kind != ci.CursorKind.FIELD_DECL:
                continue
            marks = [
                c.spelling
                for c in member.get_children()
                if c.kind == ci.CursorKind.ANNOTATE_ATTR and c.spelling.startswith(FIELD_MARK)
            ]
            if not marks:
                continue

            explicit, metadata = split_core_type(strip_annotation(marks[0], FIELD_MARK))
            inferred = infer_core_type(member.type, reflected_names)

            core_type = explicit
            struct_type = None
            if core_type is None:
                if inferred is None:
                    raise ValueError(
                        f"{node.spelling}::{member.spelling} has type "
                        f"'{member.type.spelling}', which the Reflector cannot map to a CoreType. "
                        f"Name one explicitly, e.g. TUCANO_FIELD(Float, ...)."
                    )
                core_type = inferred[0]

            if core_type == "Struct":
                struct_type = normalise_spelling(member.type.spelling).split("::")[-1]

            reflected.fields.append(Field(member.spelling, core_type, metadata, struct_type))

        if reflected.fields:
            out_types.append(reflected)


BANNER = [
    "// GENERATED by Tools/Reflector/reflector.py — do not edit.",
    "//",
    "// Regenerate with: cmake --build <dir> --target TucanoReflect",
    "// Committed to the repository on purpose: a machine without the libclang package still has to",
    "// build, so the normal build only *checks* this file and fails loudly when it is stale.",
]


def emit_header(types, headers):
    """The registration itself.

    A header, not a .cpp, because TUCANO_REFLECT_TYPE_BEGIN also specialises TypeName<T> — and that
    has to be visible wherever someone writes grid.draw(material) or defineRules<Material>(). Put
    it in a source file and those call sites stop compiling.
    """
    lines = list(BANNER)
    lines.append("")
    lines.append("#pragma once")
    lines.append("")
    lines.append('#include "Core/TypeSystem/TypeRegistry.h"')
    for header in headers:
        lines.append(f'#include "{header}"')
    lines.append("")

    for reflected in types:
        lines.append(f"TUCANO_REFLECT_TYPE_BEGIN({reflected.name})")
        for field in reflected.fields:
            if field.core_type == "Struct":
                macro = f"\tTUCANO_PROPERTY_STRUCT({field.name}, {field.struct_type}"
            else:
                macro = f"\tTUCANO_PROPERTY({field.name}, {field.core_type}"
            if field.metadata:
                macro += f", {field.metadata}"
            macro += ")"
            lines.append(macro)
        lines.append(f"TUCANO_REFLECT_TYPE_END({reflected.name})")
        lines.append("")

    return "\n".join(lines)


def emit_source(header_name):
    """Linker anchor.

    Registration runs from inline variables, which only run if this object file is kept — and
    nothing references it, so the linker is free to drop it out of a static library and take every
    registration with it, silently. TypeRegistry.cpp takes the address of the anchor below. Same
    failure mode as CP-21: code that is correct but never reached is not correct.
    """
    lines = list(BANNER)
    lines.append("")
    lines.append(f'#include "{header_name}"')
    lines.append("")
    lines.append("namespace tucano {")
    lines.append("extern const int kGeneratedReflectionAnchor;")
    lines.append("const int kGeneratedReflectionAnchor = 1;")
    lines.append("} // namespace tucano")
    return "\n".join(lines)


def read_manifest(path):
    """One header per line, relative to src/. Blank lines and # comments ignored."""
    entries = []
    with open(path, "r", encoding="utf-8") as handle:
        for raw in handle:
            line = raw.strip()
            if line and not line.startswith("#"):
                entries.append(line)
    return entries


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--src-root", required=True)
    parser.add_argument("--compile-args", required=True,
                        help="File with one clang argument per line.")
    parser.add_argument("--check", action="store_true",
                        help="Compare with the file on disk instead of writing it.")
    args = parser.parse_args()

    try:
        import clang.cindex as ci
    except ImportError:
        if args.check:
            # A check that cannot run is not a pass. Saying "up to date" here would be the same
            # kind of guard that never fires as the dangling-device test in CP-20b: technically
            # green, actually blind. The generate path below still degrades quietly, because a
            # build has to work on a machine without the package; a check does not.
            print("reflector: cannot verify — the libclang package is not installed for "
                  f"{sys.executable}. Install it (pip install libclang) or skip this target.",
                  file=sys.stderr)
            return 2
        print("reflector: the libclang package is not installed — keeping the committed output.",
              file=sys.stderr)
        return 0

    with open(args.compile_args, "r", encoding="utf-8") as handle:
        compile_args = [line.rstrip("\n") for line in handle if line.strip()]

    headers = read_manifest(args.manifest)
    index = ci.Index.create()

    # Two passes: the first only learns which type names are reflected, because a field of a nested
    # struct can only be recognised as Struct once that struct is known. Header order in the
    # manifest should not decide whether a field is editable.
    reflected_names = set()
    parsed = []
    for relative in headers:
        absolute = os.path.join(args.src_root, relative)
        translation_unit = index.parse(
            absolute, args=compile_args,
            options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
        fatal = [d for d in translation_unit.diagnostics if d.severity >= 3]
        if fatal:
            print(f"reflector: {relative} did not parse cleanly:", file=sys.stderr)
            for diagnostic in fatal[:5]:
                print(f"  {diagnostic.spelling} @ {diagnostic.location}", file=sys.stderr)
            return 1
        parsed.append((relative, absolute, translation_unit))
        for node in translation_unit.cursor.walk_preorder():
            if node.kind in (ci.CursorKind.STRUCT_DECL, ci.CursorKind.CLASS_DECL) and \
               node.is_definition() and node.location.file is not None and \
               os.path.normcase(os.path.abspath(node.location.file.name)) == \
               os.path.normcase(os.path.abspath(absolute)):
                if any(c.kind == ci.CursorKind.ANNOTATE_ATTR and c.spelling.startswith(TYPE_MARK)
                       for c in node.get_children()):
                    reflected_names.add(node.spelling)

    types = []
    for relative, absolute, translation_unit in parsed:
        try:
            collect(translation_unit.cursor, absolute, types, reflected_names)
        except ValueError as error:
            print(f"reflector: {relative}: {error}", file=sys.stderr)
            return 1

    if not types:
        print("reflector: no annotated types found — check the manifest.", file=sys.stderr)
        return 1

    header_path = args.out
    source_path = os.path.splitext(args.out)[0] + ".cpp"
    outputs = {
        header_path: emit_header(types, headers) + "\n",
        source_path: emit_source(os.path.basename(header_path)) + "\n",
    }

    if args.check:
        for path, text in outputs.items():
            existing = ""
            if os.path.exists(path):
                with open(path, "r", encoding="utf-8") as handle:
                    existing = handle.read()
            if existing != text:
                print(f"reflector: {path} is stale — run the TucanoReflect target.", file=sys.stderr)
                return 1
        print(f"reflector: generated reflection is up to date ({len(types)} types).")
        return 0

    for path, text in outputs.items():
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(text)
    total_fields = sum(len(t.fields) for t in types)
    print(f"reflector: wrote {header_path} — {len(types)} types, {total_fields} properties.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
