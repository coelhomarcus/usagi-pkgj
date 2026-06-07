# PKGj Source Code Documentation

## Overview

PKGj (toaster-code/pkgj) is a fork of the original [blastrock/pkgj](https://github.com/blastrock/pkgj) homebrew application for the PlayStation Vita. It enables downloading and installing PKG files (PS Vita packages) using TSV/DB sources, BGDL (Background Downloader), and NoNpDrm for bypassing DRM restrictions. The application is written in C++17, features an ImGui-based UI, and is cross-compiled using VitaSDK via Conan and CMake.

This documentation provides a detailed breakdown of the `/src/` directory structure, including the purpose of each file, key classes, functions, and their interactions.

## Architecture

The application follows a modular C++ structure with the following high-level components:

- **Main Entry Point**: `pkgi.cpp` - Handles initialization, main loop, UI rendering, and state management.
- **Database Management**: Manages title databases from remote sources.
- **Downloading and Installation**: Handles package downloads, BGDL integration, and installation logic.
- **User Interface**: ImGui-based UI for navigation, menus, dialogs, and game views.
- **Utilities and Platform Abstractions**: HTTP, file I/O, crypto, threading, and Vita-specific functions.
- **Configuration**: Loading and saving user settings.

The main loop in `pkgi.cpp` runs a state machine (StateMain, StateRefreshing, StateError) to handle different application states. It integrates ImGui for UI, processes input, and delegates to specific modules for tasks like refreshing databases or downloading packages.

## File Structure and Descriptions

### Core Files

- **`pkgi.cpp` / `pkgi.hpp`**: Main application logic. `pkgi.hpp` defines platform abstractions, input handling, UI functions, and utility macros. `pkgi.cpp` contains the `main()` function, state management, UI rendering loops (`pkgi_do_head()`, `pkgi_do_main()`, `pkgi_do_tail()`), and integration with other modules. Key functions include `pkgi_start_download()`, `pkgi_refresh_list()`, and mode switching.

- **`config.cpp` / `config.hpp`**: Configuration management. `Config` struct holds settings like sort options, URLs for different content types (games, DLCs, etc.), and filters. Functions `pkgi_load_config()` and `pkgi_save_config()` handle persistence.

### Database Modules

- **`db.cpp` / `db.hpp`**: Core database handling for titles. `TitleDatabase` class manages loading, updating, and querying title lists from remote TSV sources. Supports filtering, sorting, and searching. Key enums: `DbPresence` (installation status), `DbSort`, `Mode` (content types like Games, DLCs). Functions like `reload()`, `update()`, and `get()`.

- **`comppackdb.cpp` / `comppackdb.hpp`**: Compatibility pack database. `CompPackDatabase` handles updates for game and update compatibility packs. Used for patches and base games.

### Downloading and Installation

- **`downloader.cpp` / `downloader.hpp`**: Asynchronous download manager. `Downloader` class queues and processes downloads using threads. Supports different types (`Type` enum: Game, Dlc, etc.). Integrates with BGDL for background downloads. Key methods: `add()`, `get_current_download()`, progress tracking.

- **`download.cpp` / `download.hpp`**: Low-level download logic. Handles HTTP downloads, resume, and error handling. Classes like `DownloadError` for exceptions.

- **`install.cpp` / `install.hpp`**: Installation logic. Functions for installing packages, handling PSP/PSX conversions, and compatibility packs. Struct `CompPackVersion` for version management.

- **`bgdl.cpp` / `bgdl.hpp`**: Background Downloader (BGDL) integration. Functions to start BGDL tasks for Vita's native download system.

### User Interface

- **`imgui.cpp` / `imgui.hpp`**: ImGui integration. Initializes ImGui with Vita fonts and renders UI elements. `init_imgui()` sets up fonts; `pkgi_imgui_render()` draws ImGui data.

- **`menu.cpp` / `menu.hpp`**: Menu system. Handles navigation menus, likely for settings or mode selection.

- **`dialog.cpp` / `dialog.hpp`**: Dialog boxes. `Response` struct for user interactions. Functions for error dialogs, confirmations, and input.

- **`gameview.cpp` / `gameview.hpp`**: Game detail view. `GameView` class displays information about selected games, possibly with images or metadata.

### Networking and HTTP

- **`vitahttp.cpp` / `vitahttp.hpp`**: Vita-specific HTTP client. `VitaHttp` class extends `Http` for PS Vita networking.

- **`filehttp.cpp` / `filehttp.hpp`**: File-based HTTP? `FileHttp` class, possibly for local file handling.

- **`http.hpp`**: Base HTTP interface. `Http` class and `HttpError` exception.

### File and Data Handling

- **`vitafile.cpp`**: Vita file I/O operations.

- **`file.hpp`**: File utilities, enum `InodeType`.

- **`extractzip.cpp` / `extractzip.hpp`**: ZIP extraction, likely for compatibility packs.

- **`sfo.cpp` / `sfo.hpp`**: SFO (System File Object) parsing, used for Vita package metadata.

- **`puff.c` / `puff.h`**: DEFLATE decompression library (from zlib).

### Cryptography and Security

- **`zrif.cpp` / `zrif.hpp`**: ZRIF (zRIF) handling. Functions to decode zRIF strings into RIF (Rights Information File) for DRM.

- **`aes128.cpp` / `aes128.hpp`**: AES-128 encryption/decryption.

- **`sha256.cpp` / `sha256.hpp`**: SHA-256 hashing.

### Platform and Utilities

- **`vita.cpp`**: Vita-specific platform functions (battery, free space, etc.).

- **`psx.cpp` / `psx.hpp`**: PSX (PS1) game handling, including PBP conversion.

- **`psm.hpp`**: PSM (PlayStation Mobile) related constants.

- **`thread.hpp`**: Threading utilities. Classes `Mutex`, `Cond`, `Thread`, `ScopeProcessLock`.

- **`utils.hpp`**: General utilities.

- **`log.hpp`**: Logging macros.

- **`update.cpp` / `update.hpp`**: Application update checking.

- **`patchinfo.cpp` / `patchinfo.hpp` / `patchinfofetcher.cpp` / `patchinfofetcher.hpp`**: Patch information fetching. `PatchInfo` struct, `PatchInfoFetcher` class.

- **`imagefetcher.cpp` / `imagefetcher.hpp`**: Image downloading/caching (game cover).

- **`filedownload.cpp` / `filedownload.hpp`**: File download utilities.

- **`cli.cpp`**: Command-line interface for debugging.

- **`simulator.cpp`**: Simulator mode, perhaps for PC testing.

- **`sqlite.hpp`**: SQLite wrapper.

- **`style.h`**: UI style definitions.

## Key Classes and Interactions

- **TitleDatabase**: Loads and manages title lists. Interacts with HTTP for updates, filters based on config.

- **Downloader**: Queues downloads, uses threads for async processing. Calls refresh callbacks on completion.

- **GameView**: Displays game details, possibly fetches images via ImageFetcher.

- **Http / VitaHttp**: Base for all network operations. Used by databases and downloaders.

- **DbItem**: Struct representing a title, with fields like titleid, name, size, presence.

- **DownloadItem**: Struct for queued downloads, including RIF data.

The UI (ImGui) is rendered in the main loop, with menus, dialogs, and views overlaying the main list.

## Build and Dependencies

Built with CMake and Conan for VitaSDK. Dependencies include Vita2D, ImGui, SQLite, and crypto libraries. Conan manages packages like Boost, BZip2, cereal.

## Differences from Original blastrock/pkgj

This fork include enhancements like improved UI, additional modes, some bug fixes and probably new bugs. refer to commit history for modifications.

## Usage

The app runs on PS Vita with HENkaku. Users select modes (Games, DLCs, etc.), refresh lists, and download/install packages. BGDL allows background downloads.

For development, use the provided CMake/Conan setup to build for Vita.