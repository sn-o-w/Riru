#include <sys/mman.h>
#include <dlfcn.h>
#include <dl.h>
#include <link.h>
#include <string>
#include <magisk.h>
#include "hide_utils.h"
#include "wrap.h"
#include "logging.h"
#include "module.h"

namespace Hide {

    struct hide_data {
        const char **paths;
        int paths_count;
    };

    static int callback(struct dl_phdr_info *info, size_t size, void *_data) {
        auto data = (hide_data *) _data;

        for (int i = 0; i < data->paths_count; i++) {
            if (strcmp(data->paths[i], info->dlpi_name) == 0) {
                memset((void *) info->dlpi_name, 0, strlen(data->paths[i]));
                LOGD("hide %s from dl_iterate_phdr", data->paths[i]);
                return 0;
            }
        }
        return 0;
    }

    static void HidePathsFromObjects(const char **paths, int paths_count) {
        auto data = hide_data{
                .paths = paths,
                .paths_count = paths_count
        };
        dl_iterate_phdr(callback, &data);
    }

    static void HidePathsFromMaps(const char **paths, int paths_count) {
        auto hide_lib_path = Magisk::GetPathForSelfLib("libriruhide.so");

        // load riruhide.so and run the hide
        LOGD("dlopen libriruhide");
        auto handle = dlopen_ext(hide_lib_path.c_str(), 0);
        if (!handle) {
            LOGE("dlopen %s failed: %s", hide_lib_path.c_str(), dlerror());
            return;
        }
        using riru_hide_t = int(const char **names, int names_count);
        auto *riru_hide = (riru_hide_t *) dlsym(handle, "riru_hide");
        if (!riru_hide) {
            LOGE("dlsym failed: %s", dlerror());
            return;
        }

        LOGD("do hide");
        riru_hide(paths, paths_count);

        // cleanup riruhide.so
        LOGD("dlclose");
        if (dlclose(handle) != 0) {
            LOGE("dlclose failed: %s", dlerror());
            return;
        }

        procmaps_iterator *maps = pmparser_parse(-1);
        if (maps == nullptr) {
            LOGE("cannot parse the memory map");
            return;
        }

        procmaps_struct *maps_tmp;
        while ((maps_tmp = pmparser_next(maps)) != nullptr) {
            if (!strstr(maps_tmp->pathname, "/libriruhide.so")) continue;

            auto start = (uintptr_t) maps_tmp->addr_start;
            auto end = (uintptr_t) maps_tmp->addr_end;
            auto size = end - start;
            LOGV("%" PRIxPTR"-%" PRIxPTR" %s %ld %s", start, end, maps_tmp->perm, maps_tmp->offset, maps_tmp->pathname);
            munmap((void *) start, size);
        }
        pmparser_free(maps);
    }

    void DoHide(bool objects, bool maps) {
        auto self_path = Magisk::GetPathForSelfLib("libriru.so");
        auto modules = get_modules();
        auto names = (const char **) malloc(sizeof(char *) * modules->size());
        int names_count = 0;
        for (auto module : *get_modules()) {
            if (strcmp(module->id, MODULE_NAME_CORE) == 0) {
                names[names_count] = self_path.c_str();
            } else if (module->supportHide) {
                names[names_count] = module->path;
            } else {
                LOGI("module %s does not support hide", module->id);
                continue;
            }
            names_count += 1;
        }
        if (objects) Hide::HidePathsFromObjects(names, names_count);
        if (maps) Hide::HidePathsFromMaps(names, names_count);
        free(names);
    }
}