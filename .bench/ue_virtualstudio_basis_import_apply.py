import json
import os
import traceback

import unreal


WORKDIR = os.environ.get("BASIS_VS_WORKDIR", "E:/UEBasisWork/BasisConversion/VirtualStudio")
ENCODED_MANIFEST_PATH = os.path.join(WORKDIR, "virtualstudio_basis_encoded_manifest.json")
RESULT_PATH = os.path.join(WORKDIR, "virtualstudio_basis_apply_result.json")
MAP_PATH = os.environ.get("BASIS_VS_MAP", "/Game/Virtual_Studio_Kit/Maps/TrackerlessStudio")


def unreal_enum_type(*names):
    for name in names:
        enum_type = getattr(unreal, name, None)
        if enum_type is not None:
            return enum_type
    raise RuntimeError(f"Unable to find Unreal enum type: {names}")


def enum_value(enum_type, names):
    for name in names:
        if hasattr(enum_type, name):
            return getattr(enum_type, name)
    raise RuntimeError(f"Unable to find enum value {names} on {enum_type}")


SEMANTIC = {
    "Color": lambda: enum_value(unreal_enum_type("BasisTextureSemantic", "EBasisTextureSemantic"), ("COLOR", "Color")),
    "NormalMap": lambda: enum_value(unreal_enum_type("BasisTextureSemantic", "EBasisTextureSemantic"), ("NORMAL_MAP", "NormalMap")),
    "ColorWithAlpha": lambda: enum_value(unreal_enum_type("BasisTextureSemantic", "EBasisTextureSemantic"), ("COLOR_WITH_ALPHA", "ColorWithAlpha")),
    "Data": lambda: enum_value(unreal_enum_type("BasisTextureSemantic", "EBasisTextureSemantic"), ("DATA", "Data")),
}


def load_placeholder(path_candidates):
    for path in path_candidates:
        if not unreal.EditorAssetLibrary.does_asset_exist(path):
            continue
        asset = unreal.EditorAssetLibrary.load_asset(path)
        if asset:
            return asset
    raise RuntimeError(f"Unable to load any placeholder texture: {path_candidates}")


def placeholder_for_semantic(semantic, color_placeholder, normal_placeholder):
    return normal_placeholder if semantic == "NormalMap" else color_placeholder


def import_basis_textures(encoded_manifest):
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    destination_path = encoded_manifest["destination_path"]
    imported = {}

    for source_texture, info in sorted(encoded_manifest["textures"].items()):
        task = unreal.AssetImportTask()
        task.filename = info["ktx2_path"]
        task.destination_path = destination_path
        task.destination_name = info["asset_name"]
        task.automated = True
        task.save = True
        task.replace_existing = True
        asset_tools.import_asset_tasks([task])

        basis_path = f"{destination_path}/{info['asset_name']}"
        basis = unreal.EditorAssetLibrary.load_asset(basis_path)
        if not basis:
            raise RuntimeError(f"Failed to import Basis asset: {basis_path}")

        basis.set_editor_property("texture_semantic", SEMANTIC[info["semantic"]]())
        basis.set_editor_property("runtime_storage_mode", enum_value(unreal_enum_type("BasisRuntimeStorageMode", "EBasisRuntimeStorageMode"), ("FOOTPRINT_OPTIMIZED", "FootprintOptimized")))
        basis.set_editor_property("native_target_profile", enum_value(unreal_enum_type("BasisNativeTargetProfile", "EBasisNativeTargetProfile"), ("DESKTOP_BC", "DesktopBC")))
        unreal.EditorAssetLibrary.save_loaded_asset(basis)
        imported[source_texture] = basis

    return imported


def load_existing_basis_textures(encoded_manifest):
    destination_path = encoded_manifest["destination_path"]
    imported = {}
    missing = []
    for source_texture, info in sorted(encoded_manifest["textures"].items()):
        basis_path = f"{destination_path}/{info['asset_name']}"
        basis = unreal.EditorAssetLibrary.load_asset(basis_path)
        if basis:
            imported[source_texture] = basis
        else:
            missing.append(basis_path)
    if missing:
        raise RuntimeError(f"Missing imported Basis assets: {missing[:10]}")
    return imported


def prune_unlisted_basis_textures(encoded_manifest):
    destination_path = encoded_manifest["destination_path"]
    keep = {f"{destination_path}/{info['asset_name']}" for info in encoded_manifest["textures"].values()}
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    assets = registry.get_assets_by_path(destination_path, recursive=True)

    pruned = []
    failed = []
    for data in assets:
        asset_path = str(data.package_name)
        if asset_path in keep:
            continue
        try:
            if unreal.EditorAssetLibrary.delete_asset(asset_path):
                pruned.append(asset_path)
            else:
                failed.append(asset_path)
        except Exception:
            failed.append(asset_path)
    return pruned, failed


def set_material_placeholder(material, parameter_name, placeholder):
    class_name = material.get_class().get_name()
    if class_name == "MaterialInstanceConstant":
        unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(material, unreal.Name(parameter_name), placeholder)
        return True

    if class_name != "Material":
        return False

    changed = False
    try:
        expressions = list(material.get_editor_property("expressions"))
    except Exception:
        expressions = []
    for expression in expressions:
        if "TextureSampleParameter" not in expression.get_class().get_name():
            continue
        try:
            expression_parameter_name = str(expression.get_editor_property("parameter_name"))
        except Exception:
            continue
        if expression_parameter_name == parameter_name:
            expression.set_editor_property("texture", placeholder)
            changed = True
    if changed:
        unreal.MaterialEditingLibrary.recompile_material(material)
    return changed


def modify_materials(encoded_manifest, color_placeholder, normal_placeholder):
    modified = set()
    warnings = []
    for binding in encoded_manifest["bindings"]:
        material = unreal.EditorAssetLibrary.load_asset(binding["material"])
        if not material:
            warnings.append(f"Missing material {binding['material']}")
            continue
        changed = False
        for param in binding["parameters"]:
            placeholder = placeholder_for_semantic(param["semantic"], color_placeholder, normal_placeholder)
            try:
                changed = set_material_placeholder(material, param["name"], placeholder) or changed
            except Exception:
                warnings.append(f"Failed to set placeholder {binding['material']}::{param['name']}\n{traceback.format_exc()}")
        if changed:
            unreal.EditorAssetLibrary.save_loaded_asset(material)
            modified.add(binding["material"])
    return sorted(modified), warnings


def destroy_existing_binders():
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    removed = 0
    for actor in list(subsystem.get_all_level_actors()):
        if actor.get_class().get_name() == "BasisTextureMaterialBinder":
            subsystem.destroy_actor(actor)
            removed += 1
    return removed


def add_binder_actor(encoded_manifest, imported):
    unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
    destroy_existing_binders()

    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actor = subsystem.spawn_actor_from_class(unreal.BasisTextureMaterialBinder, unreal.Vector(0.0, 0.0, 120.0), unreal.Rotator(0.0, 0.0, 0.0))
    actor.set_actor_label("BasisTextureMaterialBinder")
    actor.set_editor_property("apply_to_world_on_begin_play", True)
    actor.set_editor_property("warm_native_caches_before_binding", False)

    bindings = []
    for binding_record in encoded_manifest["bindings"]:
        material = unreal.EditorAssetLibrary.load_asset(binding_record["material"])
        if not material:
            continue
        params = []
        for param_record in binding_record["parameters"]:
            basis = imported.get(param_record["texture"])
            if not basis:
                continue
            param = unreal.BasisMaterialTextureParameter()
            param.set_editor_property("parameter_name", unreal.Name(param_record["name"]))
            param.set_editor_property("basis_texture", basis)
            params.append(param)
        if not params:
            continue
        binding = unreal.BasisMaterialTextureBinding()
        binding.set_editor_property("source_material", material)
        binding.set_editor_property("texture_parameters", params)
        bindings.append(binding)

    actor.set_editor_property("material_bindings", bindings)
    unreal.EditorLoadingAndSavingUtils.save_current_level()
    return len(bindings)


def main():
    with open(ENCODED_MANIFEST_PATH, "r", encoding="utf-8") as f:
        encoded_manifest = json.load(f)

    pruned_assets = []
    prune_failed = []
    if os.environ.get("BASIS_VS_PRUNE_UNLISTED") == "1":
        unreal.EditorLoadingAndSavingUtils.load_map(MAP_PATH)
        removed_binders = destroy_existing_binders()
        if removed_binders:
            unreal.EditorLoadingAndSavingUtils.save_current_level()
        pruned_assets, prune_failed = prune_unlisted_basis_textures(encoded_manifest)

    if os.environ.get("BASIS_VS_SKIP_IMPORT") == "1":
        imported = load_existing_basis_textures(encoded_manifest)
    else:
        imported = import_basis_textures(encoded_manifest)

    if os.environ.get("BASIS_VS_SKIP_MATERIALS") == "1":
        modified_materials, warnings = [], []
    else:
        color_placeholder = load_placeholder((
            "/Engine/EngineResources/DefaultTexture",
            "/Engine/EngineResources/WhiteSquareTexture",
        ))
        normal_placeholder = load_placeholder((
            "/Engine/EngineResources/DefaultNormal",
            "/Engine/EngineMaterials/DefaultNormal",
            "/Engine/EngineResources/DefaultTexture",
        ))
        modified_materials, warnings = modify_materials(encoded_manifest, color_placeholder, normal_placeholder)
    material_binding_count = add_binder_actor(encoded_manifest, imported)

    result = {
        "imported_basis_textures": len(imported),
        "modified_materials": len(modified_materials),
        "material_bindings_on_actor": material_binding_count,
        "pruned_unlisted_basis_textures": len(pruned_assets),
        "prune_failed": prune_failed[:50],
        "warnings": warnings,
    }
    with open(RESULT_PATH, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2, ensure_ascii=False)

    unreal.log(f"VirtualStudio Basis import/apply result: {result}")
    if warnings:
        for warning in warnings[:20]:
            unreal.log_warning(warning)


if __name__ == "__main__":
    try:
        main()
    finally:
        unreal.SystemLibrary.quit_editor()
