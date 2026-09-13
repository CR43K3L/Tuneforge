cmake_minimum_required(VERSION 3.21)
project(Tuneforge VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY        ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG  ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

add_compile_definitions(
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    TF_VERSION="${PROJECT_VERSION}"
    # Doit etre defini avant toute inclusion d'imgui.h, y compris indirecte :
    # le definir globalement est la seule facon fiable de le garantir.
    IMGUI_DEFINE_MATH_OPERATORS
)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /EHsc /Zc:preprocessor)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # Runtime statique : le binaire distribue ne depend pas du VC++ Redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra -Wno-unknown-pragmas)
endif()

# ---------------------------------------------------------------------------
# Coeur : toute la logique, sans UI. Reutilisable par la future interface ImGui.
# ---------------------------------------------------------------------------
add_library(tfcore STATIC
    src/common/util.cpp
    src/common/json.cpp
    src/platform/winreg.cpp
    src/platform/power.cpp
    src/platform/elevation.cpp
    src/hw/hardware.cpp
    src/hw/sensors.cpp
    src/hw/nvapi.cpp
    src/hw/gpu.cpp
    src/hw/adl.cpp
    src/core/tweaks.cpp
    src/core/engine.cpp
)
target_include_directories(tfcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tfcore PUBLIC
    powrprof
    dxgi
    advapi32
    shell32
    ole32
    shlwapi
    version
    iphlpapi
    psapi
    pdh
)

# ---------------------------------------------------------------------------
# CLI principal
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Ressources Windows : icone et informations de version
# ---------------------------------------------------------------------------
# Sans elles, les trois executables s'affichent comme des programmes
# generiques dans l'explorateur, la barre des taches et le gestionnaire de
# taches — et un outil sans nom ni editeur qui demande les droits
# administrateur inspire la mefiance, a juste titre.
function(tf_add_resources target description)
    set(TF_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/tuneforge.ico")
    set(TF_NAME "${target}")
    set(TF_DESCRIPTION "${description}")
    set(rc "${CMAKE_CURRENT_BINARY_DIR}/resources/${target}.rc")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/resources/tuneforge.rc.in" "${rc}" @ONLY)
    target_sources(${target} PRIVATE "${rc}")
endfunction()

add_executable(tuneforge src/cli/main.cpp)
target_link_libraries(tuneforge PRIVATE tfcore)
tf_add_resources(tuneforge "Tuneforge - ligne de commande")

# ---------------------------------------------------------------------------
# Outil de secours autonome : restaure tout sans dependre de l'app principale.
# ---------------------------------------------------------------------------
add_executable(tuneforge-reset src/reset/main.cpp)
target_link_libraries(tuneforge-reset PRIVATE tfcore)
tf_add_resources(tuneforge-reset "Tuneforge - restauration d'urgence")

# ---------------------------------------------------------------------------
# Dear ImGui — rendu Direct3D 11
# ---------------------------------------------------------------------------
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)

if(EXISTS ${IMGUI_DIR}/imgui.cpp)
    add_library(imgui STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )
    target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
    target_link_libraries(imgui PUBLIC d3d11 dxgi dwmapi)
    if(MSVC)
        # Code tiers : on ne le compile pas sous nos avertissements.
        target_compile_options(imgui PRIVATE /W1)
    endif()

    # -----------------------------------------------------------------------
    # Interface graphique
    # -----------------------------------------------------------------------
    add_executable(tuneforge-gui WIN32
        src/ui/main_gui.cpp
        src/ui/app.cpp
        src/ui/theme.cpp
        src/ui/widgets.cpp
        src/ui/window.cpp
    )
    target_link_libraries(tuneforge-gui PRIVATE tfcore imgui)
    tf_add_resources(tuneforge-gui "Tuneforge")

    # L interface demande l elevation des son lancement. Elle ne sert a rien
    # sans : chaque ecriture GPU comme chaque reglage Windows la reclame, et
    # relancer a mi-parcours faisait perdre l etat de la page en cours.
    # La ligne de commande, elle, garde asInvoker : « detect », « list » et
    # « monitor » n ont aucune raison d ouvrir une invite UAC.
    # -DTF_GUI_REQUIRE_ADMIN=OFF produit un binaire sans elevation. Reserve au
    # developpement : il permet de verifier le rendu sans declencher une invite
    # UAC a chaque lancement. Ce n est pas la configuration livree.
    option(TF_GUI_REQUIRE_ADMIN "L interface exige l elevation des son lancement" ON)
    if(MSVC AND TF_GUI_REQUIRE_ADMIN)
        set_target_properties(tuneforge-gui PROPERTIES
            LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
    endif()
else()
    message(WARNING
        "Dear ImGui absent de third_party/imgui — l'interface graphique ne sera pas compilee.\n"
        "  git clone --depth 1 --branch v1.92.3 https://github.com/ocornut/imgui third_party/imgui")
endif()
