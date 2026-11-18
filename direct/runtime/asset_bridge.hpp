#pragma once

// The returned path lives until the next call to the same function on this
// thread. Null means absent or rejected; never fall back to a process path.
extern "C" int cp2_initialize_assets();
extern "C" const char* cp2_asset_read_path(const char* relative);
extern "C" const char* cp2_asset_write_path(const char* relative, int preserve);
extern "C" int cp2_asset_dirlist(const char* pattern, int mode, void* target,
                                void (*append)(void*, const char*));
extern "C" int cp2_asset_chdir(const char* path);
extern "C" const char* cp2_asset_current_directory();
extern "C" int cp2_assets_driver_test();
