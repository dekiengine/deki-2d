# Module descriptor for deki-engine auto-discovery
set(MODULE_DISPLAY_NAME "2D")
set(MODULE_PREFIX "Deki2D")
set(MODULE_UPPER "2D")
set(MODULE_TARGET "deki-2d")
set(MODULE_FILE_PREFIX "2D")
set(MODULE_SOURCES
    GradientComponent.cpp
    SpriteComponent.cpp
    ScrollComponent.cpp
    RollerComponent.cpp
    ButtonComponent.cpp
    ButtonStyleComponent.cpp
    TextComponent.cpp
    AnimationComponent.cpp
    ClipComponent.cpp
    SortingGroupComponent.cpp
)
set(MODULE_ENTRY Deki2DModule.cpp)
# Core infrastructure always compiled into deki-engine-core (needed by AssetManager, etc.)
set(MODULE_CORE_SOURCES
    Sprite.cpp
    Texture2D.cpp
    BitmapFont.cpp
    FrameAnimationMsgPack.cpp
)
set(MODULE_EDITOR_SOURCES
    editor/ClipCustomEditor.cpp
    editor/ScrollCustomEditor.cpp
    editor/ScrollElementCustomEditor.cpp
    editor/ButtonCustomEditor.cpp
    editor/FontCompiler.cpp
    editor/FontFileInspector.cpp
    editor/FontSyncHandler.cpp
    editor/FrameAnimationEditor.cpp
    editor/FrameAnimationEditorWindow.cpp
    editor/GradientCustomEditor.cpp
    editor/ProceduralSpriteEditor.cpp
    editor/RollerCustomEditor.cpp
    editor/SpriteCustomEditor.cpp
    editor/SpritesheetEditorWindow.cpp
    editor/TextComponentEditor.cpp
    editor/IconFontExporter.cpp
)
set(MODULE_LINK_DEPS freetype deki-editor deki-rendering deki-input)
set(MODULE_NEEDS_IMGUI ON)
