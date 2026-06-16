import json
import os
import re
import traceback

import unreal


WORKDIR = os.environ.get("BASIS_VS_WORKDIR", "E:/UEBasisWork/BasisConversion/VirtualStudio")
CONTENT_ROOT = os.environ.get("BASIS_VS_CONTENT_ROOT", "/Game/Virtual_Studio_Kit")
EXPORT_DIR = os.path.join(WORKDIR, "exports")
MANIFEST_PATH = os.path.join(WORKDIR, "virtualstudio_basis_manifest.json")
COOKED_WHITELIST_PATH = os.environ.get("BASIS_VS_COOKED_WHITELIST", "")


def asset_class_name(asset_data):
    class_path = getattr(asset_data, "asset_class_path", None)
    if class_path:
        return str(class_path.asset_name)
    return str(asset_data.asset_class)


def package_path(obj):
    return obj.get_outermost().get_name()


def safe_name(path):
    return re.sub(r"[^A-Za-z0-9_]+", "_", path.strip("/")).strip("_")


def enum_text(value):
    return str(value).split(".")[-1]


def get_prop(obj, name, default=None):
    try:
        return obj.get_editor_property(name)
    except Exception:
        return default


def infer_semantic(texture, parameter_name):
    text = f"{package_path(texture)} {texture.get_name()} {parameter_name} {enum_text(get_prop(texture, 'compression_settings', ''))}".lower()
    if any(token in text for token in ("normal", "_nor", "_nrm")):
        return "NormalMap"
    if any(token in text for token in ("roughness", "metallic", "ambientocclusion", "occlusion", "_ao", "height", "_orm", "mask")):
        return "Data"
    has_alpha = bool(get_prop(texture, "has_alpha_channel", False))
    if has_alpha or any(token in text for token in ("opacity", "alpha", "rgba")):
        return "ColorWithAlpha"
    return "Color"


def texture_info(texture, parameter_name):
    return {
        "asset": package_path(texture),
        "object_path": texture.get_path_name(),
        "name": texture.get_name(),
        "semantic": infer_semantic(texture, parameter_name),
        "srgb": bool(get_prop(texture, "srgb", True)),
        "compression_settings": enum_text(get_prop(texture, "compression_settings", "")),
        "has_alpha": bool(get_prop(texture, "has_alpha_channel", False)),
        "size_x": int(texture.blueprint_get_size_x()) if hasattr(texture, "blueprint_get_size_x") else 0,
        "size_y": int(texture.blueprint_get_size_y()) if hasattr(texture, "blueprint_get_size_y") else 0,
    }


def get_material_texture_parameters(material):
    result = []
    mel = unreal.MaterialEditingLibrary
    try:
        names = list(mel.get_texture_parameter_names(material))
    except Exception:
        names = []

    class_name = material.get_class().get_name()
    for name in names:
        texture = None
        try:
            if class_name == "MaterialInstanceConstant":
                texture = mel.get_material_instance_texture_parameter_value(material, name)
            else:
                texture = mel.get_material_default_texture_parameter_value(material, name)
        except Exception:
            texture = None

        if texture and texture.get_class().get_name() == "Texture2D":
            result.append((str(name), texture))

    if class_name == "Material":
        try:
            expressions = list(material.get_editor_property("expressions"))
        except Exception:
            expressions = []
        seen = {(name, package_path(texture)) for name, texture in result}
        for expression in expressions:
            if "TextureSampleParameter" not in expression.get_class().get_name():
                continue
            try:
                name = str(expression.get_editor_property("parameter_name"))
                texture = expression.get_editor_property("texture")
            except Exception:
                continue
            if texture and texture.get_class().get_name() == "Texture2D":
                key = (name, package_path(texture))
                if key not in seen:
                    result.append((name, texture))
                    seen.add(key)

    return result


def load_cooked_whitelist():
    if not COOKED_WHITELIST_PATH:
        return None
    with open(COOKED_WHITELIST_PATH, "r", encoding="utf-8") as f:
        data = json.load(f)
    packages = set(data.get("packages", []))
    unreal.log(f"VirtualStudio Basis export using cooked whitelist: {COOKED_WHITELIST_PATH} ({len(packages)} packages)")
    return packages


def export_texture(texture, export_path):
    task = unreal.AssetExportTask()
    task.object = texture
    task.filename = export_path
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    task.exporter = unreal.TextureExporterPNG()
    return bool(unreal.Exporter.run_asset_export_task(task))


def main():
    os.makedirs(EXPORT_DIR, exist_ok=True)
    os.makedirs(WORKDIR, exist_ok=True)
    cooked_whitelist = load_cooked_whitelist()

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    assets = registry.get_assets_by_path(CONTENT_ROOT, recursive=True)

    materials = []
    for data in assets:
        if asset_class_name(data) in ("Material", "MaterialInstanceConstant"):
            material = unreal.EditorAssetLibrary.load_asset(str(data.package_name))
            if material:
                materials.append(material)

    textures = {}
    bindings = []
    skipped_materials_not_cooked = 0
    skipped_parameters_not_cooked = 0
    discovered_parameters = 0
    for material in materials:
        material_path = package_path(material)
        if cooked_whitelist is not None and material_path not in cooked_whitelist:
            skipped_materials_not_cooked += 1
            continue
        params = []
        for parameter_name, texture in get_material_texture_parameters(material):
            discovered_parameters += 1
            texture_path = package_path(texture)
            if cooked_whitelist is not None and texture_path not in cooked_whitelist:
                skipped_parameters_not_cooked += 1
                continue
            info = textures.get(texture_path)
            if not info:
                info = texture_info(texture, parameter_name)
                info["export_path"] = os.path.join(EXPORT_DIR, safe_name(texture_path) + ".png").replace("\\", "/")
                textures[texture_path] = info
            params.append({
                "name": parameter_name,
                "texture": texture_path,
                "semantic": info["semantic"],
            })
        if params:
            bindings.append({
                "material": material_path,
                "material_class": material.get_class().get_name(),
                "parameters": params,
            })

    exported = 0
    export_failed = 0
    for texture_path, info in sorted(textures.items()):
        texture = unreal.EditorAssetLibrary.load_asset(texture_path)
        try:
            ok = export_texture(texture, info["export_path"])
        except Exception:
            unreal.log_warning(f"Export failed with exception for {texture_path}\n{traceback.format_exc()}")
            ok = False
        info["exported"] = bool(ok)
        if ok:
            exported += 1
        else:
            export_failed += 1
            unreal.log_warning(f"Texture export failed: {texture_path}")

    manifest = {
        "content_root": CONTENT_ROOT,
        "workdir": WORKDIR.replace("\\", "/"),
        "export_dir": EXPORT_DIR.replace("\\", "/"),
        "textures": textures,
        "bindings": bindings,
        "summary": {
            "cooked_whitelist": COOKED_WHITELIST_PATH.replace("\\", "/") if COOKED_WHITELIST_PATH else "",
            "materials_discovered": len(materials),
            "materials_skipped_not_cooked": skipped_materials_not_cooked,
            "materials_with_texture_parameters": len(bindings),
            "texture_parameters_discovered": discovered_parameters,
            "texture_parameters_skipped_not_cooked": skipped_parameters_not_cooked,
            "unique_textures": len(textures),
            "exported": exported,
            "export_failed": export_failed,
        },
    }

    with open(MANIFEST_PATH, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)

    unreal.log(f"VirtualStudio Basis export manifest: {MANIFEST_PATH}")
    unreal.log(f"VirtualStudio Basis export summary: {manifest['summary']}")
    if export_failed:
        raise RuntimeError(f"{export_failed} texture exports failed")


if __name__ == "__main__":
    try:
        main()
    finally:
        unreal.SystemLibrary.quit_editor()
