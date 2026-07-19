find_package(SQLite3 REQUIRED)

add_executable(pkgj_cli
  src/annotationdb.cpp
  src/comppackdb.cpp
  src/db.cpp
  src/download.cpp
  src/extractzip.cpp
  src/filedownload.cpp
  src/logbuffer.cpp
  src/patchinfo.cpp
  src/simulator.cpp
  src/aes128.cpp
  src/sfo.cpp
  src/sha256.cpp
  src/filehttp.cpp
  src/zrif.cpp
  src/puff.c
  src/cli.cpp
)

target_link_libraries(pkgj_cli
  fmt::fmt
  Boost::headers
  SQLite::SQLite3
  cereal::cereal
  libzip::zip
)

# ─────────────────────────────────────────────────────────────────────────────
# Graphical simulator — pkgj_sim
#
# Requires:
#   libsdl2-dev  libsdl2-ttf-dev  libsdl2-image-dev  libcurl-dev
#
# Enable with:  cmake ../.. -DBUILD_SIM=ON   (inside ci/buildhost/)
# ─────────────────────────────────────────────────────────────────────────────
option(BUILD_SIM "Build the pkgj graphical SDL2 simulator" OFF)

if(BUILD_SIM)
  find_package(imgui REQUIRED)
  find_package(CURL REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SDL2       REQUIRED IMPORTED_TARGET sdl2)
  pkg_check_modules(SDL2_TTF   REQUIRED IMPORTED_TARGET SDL2_ttf)
  pkg_check_modules(SDL2_IMAGE REQUIRED IMPORTED_TARGET SDL2_image)

  add_executable(pkgj_sim
    # ── Core portable UI ─────────────────────────────────────────────────────
    src/pkgi.cpp
    src/annotationdb.cpp
    src/comppackdb.cpp
    src/config.cpp
    src/configeditor.cpp
    src/customhandler.cpp
    src/db.cpp
    src/dialog.cpp
    src/download.cpp
    src/downloader.cpp
    src/extractzip.cpp
    src/filedownload.cpp
    src/filehttp.cpp
    src/browserview.cpp
    src/gameview.cpp
    src/gridview.cpp
    src/descriptionfetcher.cpp
    src/screenshotfetcher.cpp
    src/imagefetcher.cpp
    src/curlhttp.cpp
    src/thumbnailfetcher.cpp
    src/logbuffer.cpp
    src/logviewer.cpp
    src/menu.cpp
    src/patchinfo.cpp
    src/patchinfofetcher.cpp
    src/aes128.cpp
    src/sfo.cpp
    src/sha256.cpp
    src/zrif.cpp
    src/puff.c
    # ── POSIX platform helpers ────────────────────────────────────────────────
    src/simulator.cpp
    # ── Simulator-specific implementations ───────────────────────────────────
    simulator/sdl_backend.cpp   # replaces vita.cpp  (rendering + input)
    simulator/imgui_sdl.cpp     # replaces imgui.cpp (ImGui SDL renderer)
    simulator/bgdl_stub.cpp     # replaces bgdl.cpp  (no LiveArea queue)
    simulator/install_stub.cpp  # replaces install.cpp (no pkg install)
    simulator/vitahttp_stub.cpp # replaces vitahttp.cpp (libcurl-based HTTP)
    simulator/update_stub.cpp   # replaces update.cpp (no auto-update)
    simulator/sim_file.cpp      # extra POSIX file helpers
  )

  target_compile_definitions(pkgj_sim PRIVATE PKGI_SIMULATOR)

  target_include_directories(pkgj_sim PRIVATE
    src/
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
  )

  target_link_libraries(pkgj_sim
    fmt::fmt
    Boost::headers
    SQLite::SQLite3
    cereal::cereal
    libzip::zip
    imgui::imgui
    PkgConfig::SDL2
    PkgConfig::SDL2_TTF
    PkgConfig::SDL2_IMAGE
    CURL::libcurl
  )

  message(STATUS "pkgj_sim: graphical simulator target enabled")
endif()
