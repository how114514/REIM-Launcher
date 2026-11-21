"""
Generates the R0 map and terrain materials for the REIM UE project.

Run with UnrealEditor-Cmd (see tools/m2-build.ps1):

  UnrealEditor-Cmd.exe <REIM.uproject> -run=pythonscript
    -script="<this file>" -unattended -nosplash -nop4
    -EnablePlugins=PythonScriptPlugin

Creates:
  /Game/Maps/R0            empty level (GameMode/GameInstance from DefaultEngine.ini)
  /Game/R0/M_McBlock       unlit opaque terrain material
  /Game/R0/M_McBlockTranslucent
  /Game/R0/M_Sky           solid-colour sky material (SkyColor parameter)
  /Game/R0/M_DebugPlayer   unlit player-proxy material

The materials carry no Minecraft art. They sample a runtime texture parameter
("Atlas") delivered by get_asset and a per-vertex baked color/alpha, plus
distance fog parameters fed by the Bridge. This is a generated adapter asset,
not translated Java rendering.
"""

import unreal


MAP_PATH = "/Game/Maps/R0"
MATERIAL_DIR = "/Game/R0"
OPAQUE_PATH = MATERIAL_DIR + "/M_McBlock"
TRANSLUCENT_PATH = MATERIAL_DIR + "/M_McBlockTranslucent"
SKY_PATH = MATERIAL_DIR + "/M_Sky"
PLAYER_PATH = MATERIAL_DIR + "/M_DebugPlayer"


def log(message):
    unreal.log("[REIM assets] " + message)


def ensure_content_dirs():
    unreal.EditorAssetLibrary.make_directory(MATERIAL_DIR)
    unreal.EditorAssetLibrary.make_directory("/Game/Maps")


def create_r0_map():
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(MAP_PATH):
        log("R0 map already exists; reloading")
        les.load_level(MAP_PATH)
        return
    les.new_level(MAP_PATH)
    les.save_current_level()
    log("created " + MAP_PATH)


def delete_if_exists(path):
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.EditorAssetLibrary.delete_asset(path)


def new_expr(material, cls, x, y):
    return unreal.MaterialEditingLibrary.create_material_expression(material, cls, x, y)


def make_terrain_material(path, translucent):
    delete_if_exists(path)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = asset_tools.create_asset(
        path.rsplit("/", 1)[-1], MATERIAL_DIR, unreal.Material,
        unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    # Opaque terrain is masked (alpha cutout) so leaves, grass crosses and
    # flowers keep their holes while still writing depth; plants are emitted
    # as two back-to-back quads instead of a two-sided material.
    material.set_editor_property(
        "blend_mode",
        unreal.BlendMode.BLEND_TRANSLUCENT if translucent
        else unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("two_sided", False)

    # --- atlas texture * baked vertex colour -------------------------------
    tex = new_expr(material, unreal.MaterialExpressionTextureSampleParameter2D, -1100, -200)
    tex.set_editor_property("parameter_name", "Atlas")
    tex.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)

    vcol = new_expr(material, unreal.MaterialExpressionVertexColor, -1100, 40)

    tinted = new_expr(material, unreal.MaterialExpressionMultiply, -800, -120)
    unreal.MaterialEditingLibrary.connect_material_expressions(tex, "RGB", tinted, "A")
    # VertexColor's RGB output is the unnamed first output; connecting "RGB"
    # silently fails and leaves the multiply at its default, which is why the
    # UE pass lost tint and baked light until S4.
    unreal.MaterialEditingLibrary.connect_material_expressions(vcol, "", tinted, "B")

    # --- distance fog: distance(world, camera) between FogStart/FogEnd -----
    world = new_expr(material, unreal.MaterialExpressionWorldPosition, -1400, 300)
    camera = new_expr(material, unreal.MaterialExpressionCameraPositionWS, -1400, 420)
    distance = new_expr(material, unreal.MaterialExpressionDistance, -1150, 360)
    unreal.MaterialEditingLibrary.connect_material_expressions(world, "", distance, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(camera, "", distance, "B")

    fog_start = new_expr(material, unreal.MaterialExpressionScalarParameter, -1150, 500)
    fog_start.set_editor_property("parameter_name", "FogStart")
    fog_start.set_editor_property("default_value", 5000.0)

    fog_end = new_expr(material, unreal.MaterialExpressionScalarParameter, -1150, 620)
    fog_end.set_editor_property("parameter_name", "FogEnd")
    fog_end.set_editor_property("default_value", 12000.0)

    span = new_expr(material, unreal.MaterialExpressionSubtract, -950, 560)
    unreal.MaterialEditingLibrary.connect_material_expressions(fog_end, "", span, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(fog_start, "", span, "B")

    ahead = new_expr(material, unreal.MaterialExpressionSubtract, -950, 400)
    unreal.MaterialEditingLibrary.connect_material_expressions(distance, "", ahead, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(fog_start, "", ahead, "B")

    ratio = new_expr(material, unreal.MaterialExpressionDivide, -750, 440)
    unreal.MaterialEditingLibrary.connect_material_expressions(ahead, "", ratio, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(span, "", ratio, "B")

    fog_clamped = new_expr(material, unreal.MaterialExpressionClamp, -560, 440)
    fog_clamped.set_editor_property("min_default", 0.0)
    fog_clamped.set_editor_property("max_default", 1.0)
    unreal.MaterialEditingLibrary.connect_material_expressions(ratio, "", fog_clamped, "")

    fog_strength = new_expr(material, unreal.MaterialExpressionConstant, -560, 560)
    fog_strength.set_editor_property("r", 0.85)
    fog_amount = new_expr(material, unreal.MaterialExpressionMultiply, -380, 460)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        fog_clamped, "", fog_amount, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(
        fog_strength, "", fog_amount, "B")

    fog_color = new_expr(material, unreal.MaterialExpressionVectorParameter, -380, 260)
    fog_color.set_editor_property("parameter_name", "FogColor")
    fog_color.set_editor_property("default_value", unreal.LinearColor(0.69, 0.78, 1.0, 1.0))

    final_color = new_expr(material, unreal.MaterialExpressionLinearInterpolate, -140, 120)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        tinted, "", final_color, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(
        fog_color, "RGB", final_color, "B")
    unreal.MaterialEditingLibrary.connect_material_expressions(
        fog_amount, "", final_color, "Alpha")
    unreal.MaterialEditingLibrary.connect_material_property(
        final_color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    tex_alpha = new_expr(material, unreal.MaterialExpressionMultiply, -800, 700)
    unreal.MaterialEditingLibrary.connect_material_expressions(
        tex, "A", tex_alpha, "A")
    unreal.MaterialEditingLibrary.connect_material_expressions(
        vcol, "A", tex_alpha, "B")

    if translucent:
        water_alpha = new_expr(material, unreal.MaterialExpressionConstant, -800, 820)
        water_alpha.set_editor_property("r", 0.72)
        final_alpha = new_expr(material, unreal.MaterialExpressionMultiply, -560, 760)
        unreal.MaterialEditingLibrary.connect_material_expressions(
            tex_alpha, "", final_alpha, "A")
        unreal.MaterialEditingLibrary.connect_material_expressions(
            water_alpha, "", final_alpha, "B")
        unreal.MaterialEditingLibrary.connect_material_property(
            final_alpha, "", unreal.MaterialProperty.MP_OPACITY)
    else:
        # Masked blend: alpha-test to the texture's own alpha (binary in the
        # client.jar block textures); full-cube textures are alpha 255.
        unreal.MaterialEditingLibrary.connect_material_property(
            tex_alpha, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
    log("created " + path + (" (translucent)" if translucent else " (opaque)"))


def make_sky_material():
    delete_if_exists(SKY_PATH)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = asset_tools.create_asset(
        "M_Sky", MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("two_sided", True)
    color = new_expr(material, unreal.MaterialExpressionVectorParameter, -300, 0)
    color.set_editor_property("parameter_name", "SkyColor")
    color.set_editor_property("default_value",
                              unreal.LinearColor(0.47, 0.65, 1.0, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(
        color, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(SKY_PATH, only_if_is_dirty=False)
    log("created " + SKY_PATH)


def make_player_material():
    delete_if_exists(PLAYER_PATH)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    material = asset_tools.create_asset(
        "M_DebugPlayer", MATERIAL_DIR, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    color = new_expr(material, unreal.MaterialExpressionConstant3Vector, -300, 0)
    color.set_editor_property("constant", unreal.LinearColor(0.85, 0.85, 0.9, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(
        color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_asset(PLAYER_PATH, only_if_is_dirty=False)
    log("created " + PLAYER_PATH)


def main():
    ensure_content_dirs()
    create_r0_map()
    make_terrain_material(OPAQUE_PATH, False)
    make_terrain_material(TRANSLUCENT_PATH, True)
    make_sky_material()
    make_player_material()
    log("done")


main()
