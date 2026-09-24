# 3D Movie Maker

add_library(bren)
target_sources(bren
    PRIVATE
    "${PROJECT_SOURCE_DIR}/bren/bwld.cpp"
    "${PROJECT_SOURCE_DIR}/bren/tmap.cpp"
    "${PROJECT_SOURCE_DIR}/bren/zbmp.cpp"
)

# Modern BRender already supplies its own compatible stdio/diagnostic/memory
# handlers. The 1995 copies in bren/ use older callback-structure layouts and
# would also duplicate BRender 1.4's global default-handler symbols.
if (NOT "${3DMM_BRENDER_LIBRARY}" STREQUAL "Modern")
    target_sources(bren
        PRIVATE
        "${PROJECT_SOURCE_DIR}/bren/stderr.c"
        "${PROJECT_SOURCE_DIR}/bren/stdfile.c"
    )
    if (WIN32)
        target_sources(bren PRIVATE "${PROJECT_SOURCE_DIR}/bren/stdmemwin.c")
    endif()
endif()

# BRender 1.4 still declares BrFixedMulDiv for its legacy fixed-point API,
# but its modern fixed.c no longer defines that one helper. 3DMM's fixed ABI
# reaches it through BR_MULDIV, so provide the missing compatibility symbol in
# our existing BRender glue library rather than modifying the fetched tree.
if ("${3DMM_BRENDER_LIBRARY}" STREQUAL "Modern")
    target_sources(bren PRIVATE "${PROJECT_SOURCE_DIR}/bren/brfixed_compat.cpp")
endif()

target_include_directories(bren PUBLIC "${PROJECT_SOURCE_DIR}/bren/inc")
target_link_libraries(bren
    PUBLIC
    KauaiBase
    KauaiGroup
    BRender::Libraries
)

add_library(engine)
target_sources(engine
    PRIVATE
    "${PROJECT_SOURCE_DIR}/src/engine/actor.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actredit.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actrsave.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/actrsnd.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/bkgd.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/body.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/modl.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/movie.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/msnd.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/mtrl.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/scene.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tagl.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tagman.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tbox.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tdf.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tdt.cpp"
    "${PROJECT_SOURCE_DIR}/src/engine/tmpl.cpp"
)
target_link_libraries(
    engine
    PRIVATE
    miniaudio
)
if (3DMM_GUI STREQUAL "Win32" AND NOT 3DMM_AUDIOMAN_LIBRARY STREQUAL "None")
    target_sources(engine
        PRIVATE
        "${PROJECT_SOURCE_DIR}/src/engine/srecwin.cpp"
    )
elseif(3DMM_GUI STREQUAL "SDL")
    target_sources(engine
        PRIVATE
        "${PROJECT_SOURCE_DIR}/src/engine/srecma.cpp"
    )
else()
    target_sources(engine
        PRIVATE
        "${PROJECT_SOURCE_DIR}/src/engine/srecstub.cpp"
    )
endif()

target_include_directories(engine PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/inc>)
target_link_libraries(engine
    PUBLIC
    KauaiBase
    KauaiGui
    KauaiDoc
    KauaiRichText
    bren
)
if (NOT "${3DMM_AUDIOMAN_LIBRARY}" STREQUAL "None")
    target_link_libraries(engine
        PUBLIC
        3DMMForever::AudioMan
    )
endif()

# On non-windows WIN32 is a no-op
add_executable(studio WIN32)
target_sources(studio
    PRIVATE
    "${PROJECT_SOURCE_DIR}/src/studio/ape.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/appicon.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/browser.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/configini.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/esl.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/popup.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/scnsort.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/splot.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/stdiobrw.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/stdioscb.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/studio.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/tatr.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/tgob.cpp"
    "${PROJECT_SOURCE_DIR}/src/studio/utest.cpp"
)
if (WIN32)
    target_sources(
        studio
        PRIVATE
        "${PROJECT_SOURCE_DIR}/src/studio/mminstal.cpp"
        "${PROJECT_SOURCE_DIR}/src/studio/utest.rc"
        "${PROJECT_SOURCE_DIR}/src/studio/utest.rc2"
    )
    if ("${3DMM_BRENDER_LIBRARY}" STREQUAL "Modern")
        target_sources(studio PRIVATE "${PROJECT_SOURCE_DIR}/src/studio/brmodernwin.cpp")
        # The 1.4 core/full targets do not pull renderer drivers into the
        # application.  Link glrend explicitly so BrDrv1GLBegin and the
        # public brglrend.h interface are available to the Win32 bridge.
        target_link_libraries(studio PRIVATE BRender::Drivers::GLRend opengl32 dbghelp)

        # modernbrlog1: keep linker symbols beside the installed executable.
        # The optimized build can still crash at an RVA that is difficult to
        # identify without its PDB; the MAP file gives us a compact symbol
        # reference that survives the user's normal source/version backup.
        if (MSVC)
            target_link_options(studio PRIVATE /MAP)
            add_custom_command(
                TARGET studio POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_INSTALL_PREFIX}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_FILE_DIR:studio>/3dmovie.map"
                        "${CMAKE_INSTALL_PREFIX}/3dmovie.map"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "$<TARGET_PDB_FILE:studio>"
                        "${CMAKE_INSTALL_PREFIX}/3dmovie.pdb"
                VERBATIM
            )
        endif()
    endif()
    if (MSVC AND "${3DMM_BRENDER_LIBRARY}" STREQUAL "Original")
        # perf1: compile the RGB888 TA24 clipping guard into every MSVC Original build, including RelWithDebInfo.
        # The original library's TriangleRenderPIZ2TA24 symbol is renamed to
        # TriangleRenderPIZ2TA24_orig in elib/brender/wind/brzbmxr.lib.
        target_sources(studio PRIVATE "${PROJECT_SOURCE_DIR}/bren/ta24clip.cpp")
        # tdtdiag1: inspect final RGB888 Gouraud vertex colours reaching the legacy rasterizer.
        target_sources(studio PRIVATE "${PROJECT_SOURCE_DIR}/bren/rgb888shade_diag.cpp")

        # Never leave an older installed 3dmovie.exe available after a failed
        # relink.  Also remove the incremental-link PDB/ILK before every link:
        # MSVC can otherwise abort with LNK1285 on a stale/corrupt 3dmovie.pdb.
        add_custom_command(
            TARGET studio PRE_LINK
            COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_INSTALL_PREFIX}/3dmovie.exe"
            COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_PDB_FILE:studio>"
            COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:studio>/3dmovie.ilk"
            VERBATIM
        )
        add_custom_command(
            TARGET studio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_INSTALL_PREFIX}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:studio>" "${CMAKE_INSTALL_PREFIX}/3dmovie.exe"
            VERBATIM
        )
    endif()
endif()

if(3DMM_GUI STREQUAL "SDL")
    target_sources(
        studio
        PRIVATE
        "${PROJECT_SOURCE_DIR}/src/studio/dlgsdl.cpp"
    )

    # Select portfolio
    if (WIN32 OR APPLE OR (UNIX AND NOT APPLE))
        # Use Native File Dialog Extended for file dialogs
        include(FetchNativeFileDialogExtended)
        target_sources(
            studio
            PRIVATE
            "${PROJECT_SOURCE_DIR}/src/studio/portfnative.cpp"
        )
        target_link_libraries(
            studio
            PRIVATE
            nfd::nfd
        )
    else()
        # Portfolio not supported
        target_sources(
            studio
            PRIVATE
            "${PROJECT_SOURCE_DIR}/src/studio/portfstub.cpp"
        )
    endif()
elseif(3DMM_GUI STREQUAL "Win32")
    # Use original Win32 portfolio
    target_sources(
        studio
        PRIVATE
        "${PROJECT_SOURCE_DIR}/src/studio/portfwin.cpp"
    )
endif()

target_include_directories(studio
  PUBLIC $<TARGET_PROPERTY:engine,INCLUDE_DIRECTORIES>
  "${PROJECT_BINARY_DIR}/inc"
  "${PROJECT_SOURCE_DIR}/src")

target_compile_definitions(
    studio
    PRIVATE
    NAMES # Adds names to chunks in Chunky files
)

# These are only here to ensure configure is rerun when dependencies are generated.
file(GLOB building-chunk-sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/building/*.cht")
file(GLOB studio-chunk-sources CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/studio/*.cht")

if (NOT 3DMM_PRECOMPILED_CHUNKY_PATH)
    target_chomp_sources(studio
        "${PROJECT_SOURCE_DIR}/src/studio/utest.cht"
        "${PROJECT_SOURCE_DIR}/src/studio/studio.cht"
        "${PROJECT_SOURCE_DIR}/src/building/bldghd.cht"
        "${PROJECT_SOURCE_DIR}/src/building/building.cht"
        "${PROJECT_SOURCE_DIR}/src/helpaud/helpaud.cht"
        "${PROJECT_SOURCE_DIR}/src/help/help.cht"
        "${PROJECT_SOURCE_DIR}/src/shared/shared.cht"
        "${PROJECT_SOURCE_DIR}/src/shared/sharecd.cht"
    )

    # studio.cht includes easels.cht and the other Studio chunk sources.
    # Ninja/MSVC does not reliably honor IMPLICIT_DEPENDS for those includes,
    # so content-only changes to easels.cht could leave the old studio.chk in
    # place. Make the included chunks explicit dependencies of the preprocessed
    # top-level Studio resource.
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/chomp/studio/studio.cht.i"
        APPEND
        DEPENDS ${studio-chunk-sources}
    )
endif()

target_link_libraries(studio
    PUBLIC
    KauaiBase
    KauaiGui
    KauaiGuiMain
    KauaiKid
    KauaiDlg
    engine

    iniparser-static
)
if (WIN32)
    target_link_libraries(studio
        PUBLIC
        mpr     # Required for WNetGetUser()
        vfw32   # Required for ICInfo in _wHaveICMCodec
        msacm32 # Required for acmFormatTagDetailsA in _wHaveACMCodec
        comdlg32 # Required for the native Dress/Color colour picker
    )
endif()

set_property(TARGET studio PROPERTY OUTPUT_NAME 3dmovie)

# 4DMM native model -> VXP2 converter. Keep the conversion core isolated from
# the Win32 shell so the same code can later be linked directly into 4DMM.
# GLB uses the locally maintained cgltf copy already present in BRender 1.4;
# FBX/ZIP decompression uses the locally maintained stb zlib code there too.
if (WIN32)
    add_executable(obj2vxp2 WIN32
        "${PROJECT_SOURCE_DIR}/tools/obj2vxp2/obj2vxp2_core.cpp"
        "${PROJECT_SOURCE_DIR}/tools/obj2vxp2/obj2vxp2_gltf.cpp"
        "${PROJECT_SOURCE_DIR}/tools/obj2vxp2/obj2vxp2_fbx.cpp"
        "${PROJECT_SOURCE_DIR}/tools/obj2vxp2/obj2vxp2_win.cpp"
        "${PROJECT_SOURCE_DIR}/tools/obj2vxp2/zip_read.cpp"
        "${PROJECT_SOURCE_DIR}/tools/obj2vxp2/zip_store.cpp"
    )
    target_include_directories(obj2vxp2 PRIVATE "${PROJECT_SOURCE_DIR}/tools/obj2vxp2")
    target_compile_features(obj2vxp2 PRIVATE cxx_std_17)
    target_compile_definitions(obj2vxp2 PRIVATE UNICODE _UNICODE)
    target_link_libraries(obj2vxp2 PRIVATE ole32 windowscodecs shell32 comdlg32 comctl32)
    install(TARGETS obj2vxp2 RUNTIME DESTINATION tools/obj2vxp2)
endif()

if(MSVC)
    target_link_options(studio
        BEFORE PRIVATE
        "/MANIFESTUAC:NO"
    )
    target_link_options(studio
        BEFORE PRIVATE
        "/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'"
    )

    if ("${3DMM_AUDIOMAN_LIBRARY}" STREQUAL "Original")
        # Avoid LNK4098 warning when linking original AudioMan libraries
        target_link_options(studio
            BEFORE PRIVATE
            $<$<CONFIG:DEBUG>:/NODEFAULTLIB:libcmt.lib>
        )
    endif()
endif()

set_property(
    TARGET studio
    PROPERTY 3DMM_SAMPLES
    ${studio-samples}
    "${PROJECT_SOURCE_DIR}/samples/bongo.3mm"
    "${PROJECT_SOURCE_DIR}/samples/boooooo.3mm"
    "${PROJECT_SOURCE_DIR}/samples/citytour.3mm"
    "${PROJECT_SOURCE_DIR}/samples/graveyrd.3mm"
    "${PROJECT_SOURCE_DIR}/samples/haunted.3mm"
    "${PROJECT_SOURCE_DIR}/samples/hospital.3mm"
    "${PROJECT_SOURCE_DIR}/samples/jungle.3mm"
    "${PROJECT_SOURCE_DIR}/samples/message.3mm"
    "${PROJECT_SOURCE_DIR}/samples/space.3mm"
    "${PROJECT_SOURCE_DIR}/samples/sprog.3mm"
    "${PROJECT_SOURCE_DIR}/samples/terror.3mm"
    "${PROJECT_SOURCE_DIR}/samples/thebook.3mm"
    "${PROJECT_SOURCE_DIR}/samples/thelodge.3mm"
    "${PROJECT_SOURCE_DIR}/samples/thethief.3mm"
    "${PROJECT_SOURCE_DIR}/samples/venus31.3mm"
    "${PROJECT_SOURCE_DIR}/samples/where.3mm"
)
set_property(
    TARGET studio
    PROPERTY 3DMM_PROJECTS
    "${PROJECT_SOURCE_DIR}/projects/bongo.3mm"
    "${PROJECT_SOURCE_DIR}/projects/bridge.3mm"
    "${PROJECT_SOURCE_DIR}/projects/dialogue.3mm"
    "${PROJECT_SOURCE_DIR}/projects/jog.3mm"
    "${PROJECT_SOURCE_DIR}/projects/mummy.3mm"
    "${PROJECT_SOURCE_DIR}/projects/surprise.3mm"
)

# Please pay attention to the ending / in each DIRECTORY declaration. It
# affects the destination!
install(TARGETS studio RUNTIME DESTINATION .)

if (3DMM_PRECOMPILED_CHUNKY_PATH)
    set(chunky_files
        3dmovie.chk
        bldghd.chk
        building.chk
        help.chk
        helpaud.chk
        sharecd.chk
        shared.chk
        studio.chk
    )
    foreach(chunky_file ${chunky_files})
        install(
            FILES "${3DMM_PRECOMPILED_CHUNKY_PATH}/${chunky_file}"
            DESTINATION "Microsoft Kids/3D Movie Maker"
        )
    endforeach()
else()
    # HACK(bruxisma): This is due to UTEST.CHT not generating 3DMOVIE.CHK
    #                 Renaming it to 3DMOVIE.CHT would resolve this.
    install(FILES $<FILTER:$<TARGET_PROPERTY:studio,CHOMPED_CHUNKS>,EXCLUDE,.+utest[.]chk$>
      DESTINATION "Microsoft Kids/3D Movie Maker")
    install(FILES $<FILTER:$<TARGET_PROPERTY:studio,CHOMPED_CHUNKS>,INCLUDE,.+utest[.]chk$>
      RENAME 3dmovie.chk
      DESTINATION "Microsoft Kids/3D Movie Maker")
endif()
install(FILES $<TARGET_PROPERTY:studio,3DMM_PROJECTS>
  DESTINATION "Microsoft Kids/Users/Melanie")
install(FILES $<TARGET_PROPERTY:studio,3DMM_SAMPLES>
  DESTINATION "Microsoft Kids/Users/McZee")
install(DIRECTORY "${PROJECT_SOURCE_DIR}/content-files/"
  DESTINATION "Microsoft Kids/3D Movie Maker")

if (WIN32 AND 3DMM_GUI STREQUAL "SDL")
    # Install SDL DLLs
    install(FILES $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE:SDL2_ttf::SDL2_ttf>
        DESTINATION .)

    if (FluidSynth_FOUND)
        # Install FluidSynth DLLs
        install(DIRECTORY "${FluidSynth_ROOT}/bin/" DESTINATION .
            PATTERN "*.exe" EXCLUDE)
    endif()
endif()

# Copy soundfont if required
if(NOT "${3DMM_SOUNDFONT_PATH}" STREQUAL "")
    install(FILES "${3DMM_SOUNDFONT_PATH}" DESTINATION .)
endif()
