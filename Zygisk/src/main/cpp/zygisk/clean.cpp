#include <linux/mman.h>
#include <sys/mman.h>
#include <lsplt.hpp>
#include <vector>
#include "logging.h"
#include "solist.hpp"


void reSoMap(const char *path){
    LOGD("spoofing virtual maps for %s", path);
    for (auto &map : lsplt::MapInfo::Scan()) {
        if (strstr(map.path.c_str(), path)) {
            void *addr = (void *) map.start;
            size_t size = map.end - map.start;
            void *copy = mmap(nullptr, size, PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
            if (copy == MAP_FAILED) {
                LOGE("failed to backup block %s [%p, %p]", map.path.c_str(), addr,
                     (void *) map.end);
                continue;
            }

            if ((map.perms & PROT_READ) == 0) {
                mprotect(addr, size, PROT_READ);
            }
            memcpy(copy, addr, size);
            mremap(copy, size, size, MREMAP_MAYMOVE | MREMAP_FIXED, addr);
            mprotect(addr, size, map.perms);
        }
    }
}


size_t  remove_soinfo(const char *path, size_t load, size_t unload, bool spoof_maps) {
    LOGD("cleaning trace for path %s", path);
    if (load > 0 || unload > 0) SoList::resetCounters(load, unload);
    size_t size_found = SoList::dropSoPath(path);
//    if (!(size_found !=-1) || !spoof_maps) return;
    if(size_found == -1){
        return -1;
    }

    return size_found;
}

namespace SoList {

bool initialize() {
    SandHook::ElfImg linker("/linker");
    if (!ProtectedDataGuard::setup(linker)) return false;
    LOGD("found symbol ProtectedDataGuard");

    std::string_view solist_sym_name = linker.findSymbolNameByPrefix("__dl__ZL6solist");
    if (solist_sym_name.empty()) return false;
    LOGD("found symbol name %s", solist_sym_name.data());

    std::string_view soinfo_free_name =
        linker.findSymbolNameByPrefix("__dl__ZL11soinfo_freeP6soinfo");
    if (soinfo_free_name.empty()) return false;
    LOGD("found symbol name %s", soinfo_free_name.data());

    char llvm_sufix[llvm_suffix_length + 1];

    if (solist_sym_name.length() != strlen("__dl__ZL6solist")) {
        strncpy(llvm_sufix, solist_sym_name.data() + strlen("__dl__ZL6solist"), sizeof(llvm_sufix));
    } else {
        llvm_sufix[0] = '\0';
    }

    char somain_sym_name[sizeof("__dl__ZL6somain") + sizeof(llvm_sufix)];
    snprintf(somain_sym_name, sizeof(somain_sym_name), "__dl__ZL6somain%s", llvm_sufix);

    char sonext_sym_name[sizeof("__dl__ZL6sonext") + sizeof(llvm_sufix)];
    snprintf(sonext_sym_name, sizeof(somain_sym_name), "__dl__ZL6sonext%s", llvm_sufix);

    char vdso_sym_name[sizeof("__dl__ZL4vdso") + sizeof(llvm_sufix)];
    snprintf(vdso_sym_name, sizeof(vdso_sym_name), "__dl__ZL4vdso%s", llvm_sufix);

    somain = getStaticPointer<SoInfo>(linker, somain_sym_name);
    if (somain == nullptr) return false;
    LOGD("found symbol somain");

    sonext = linker.getSymbAddress<SoInfo **>(sonext_sym_name);
    if (sonext == nullptr) return false;
    LOGD("found symbol sonext");

    auto *vdso = getStaticPointer<SoInfo>(linker, vdso_sym_name);
    if (vdso != nullptr) LOGD("found symbol vdso");

    SoInfo::get_realpath_sym = reinterpret_cast<decltype(SoInfo::get_realpath_sym)>(
        linker.getSymbAddress("__dl__ZNK6soinfo12get_realpathEv"));
    if (SoInfo::get_realpath_sym != nullptr) LOGD("found symbol get_realpath_sym");

//    SoInfo::get_soname = reinterpret_cast<decltype(SoInfo::get_soname)>(
//            linker.getSymbAddress("__dl__ZNK6soinfo10get_sonameEv"));
//    if (SoInfo::get_soname != nullptr) LOGD("found symbol get_soname");

    SoInfo::soinfo_free =
        reinterpret_cast<decltype(SoInfo::soinfo_free)>(linker.getSymbAddress(soinfo_free_name));
    if (SoInfo::soinfo_free == nullptr) return false;
    LOGD("found symbol soinfo_free");

    g_module_load_counter = reinterpret_cast<decltype(g_module_load_counter)>(
        linker.getSymbAddress("__dl__ZL21g_module_load_counter"));
    if (g_module_load_counter != nullptr) LOGD("found symbol g_module_load_counter");

    g_module_unload_counter = reinterpret_cast<decltype(g_module_unload_counter)>(
        linker.getSymbAddress("__dl__ZL23g_module_unload_counter"));
    if (g_module_unload_counter != nullptr) LOGD("found symbol g_module_unload_counter");

    solist = getStaticPointer<SoInfo>(linker, solist_sym_name.data());
    if (solist == nullptr) return false;
    LOGD("found symbol solist");

    bool size_filed_found = false;
    bool next_filed_found = false;
    const size_t linker_realpath_size = linker.name().size();
    for (size_t i = 0; i < size_block_range / sizeof(void *); i++) {
        auto possible_field = reinterpret_cast<uintptr_t>(solist) + i * sizeof(void *);
        auto possible_size_of_somain =
            *reinterpret_cast<size_t *>(reinterpret_cast<uintptr_t>(somain) + i * sizeof(void *));
        if (!size_filed_found && possible_size_of_somain < size_maximal &&
            possible_size_of_somain > size_minimal) {
            SoInfo::field_size_offset = i * sizeof(void *);
            LOGD("field_size_offset is %zu * %zu = %p", i, sizeof(void *),
                 (void *) SoInfo::field_size_offset);
            size_filed_found = true;
        }
        if (!next_filed_found &&
            (*reinterpret_cast<void **>(possible_field) == somain ||
             (vdso != nullptr && *reinterpret_cast<void **>(possible_field) == vdso))) {
            SoInfo::field_next_offset = i * sizeof(void *);
            LOGD("field_next_offset should be here %zu * %zu = %p", i, sizeof(void *),
                 (void *) SoInfo::field_next_offset);
            next_filed_found = true;
            if (SoInfo::get_realpath_sym != nullptr) break;
        }
        if (size_filed_found && next_filed_found) {
            std::string *realpath = reinterpret_cast<std::string *>(
                reinterpret_cast<uintptr_t>(solist) + i * sizeof(void *));
            if (realpath->size() == linker_realpath_size) {
                char buffer[100];
                strncpy(buffer, realpath->c_str(), linker_realpath_size);
                buffer[linker_realpath_size] = '\0';
                if (strcmp(linker.name().c_str(), buffer) == 0) {
                    SoInfo::field_realpath_offset = i * sizeof(void *);
                    LOGD("field_realpath_offset is %zu * %zu = %p", i, sizeof(void *),
                         (void *) SoInfo::field_realpath_offset);
                    break;
                }
            }
        }
    }
    return true;
}

size_t dropSoPath(const char *target_path) {
    size_t size_found = -1;
    if (solist == nullptr && !initialize()) {
        LOGE("failed to initialize solist");
        return size_found;
    }
    for (auto *iter = solist; iter; iter = iter->getNext()) {
        if (iter->getPath() && strstr(iter->getPath(), target_path)) {
            SoList::ProtectedDataGuard guard;
            size_found = iter->getSize();
            LOGD("dropping solist record for %s addr %lx with size %zu", iter->getPath(), iter,size_found);//.831488
            if (iter->getSize() > 0) {
                iter->setSize(0);
                SoInfo::soinfo_free(iter);
//                const char* soname = iter->getSoname();
//                LOGE("soinfo soname %s  soname_addr %p addr %lx",soname,soname,iter);  //  7604c573b0
//                memset(iter,0, strlen(soname));
//                LOGE("soinfo addr next 0x%x",iter->getNext());
            }
        }
    }
    return size_found;
}

void resetCounters(size_t load, size_t unload) {
    if (solist == nullptr && !initialize()) {
        LOGE("failed to initialize solist");
        return;
    }
    if (g_module_load_counter == nullptr || g_module_unload_counter == nullptr) {
        LOGD("g_module counters not defined, skip reseting them");
        return;
    }
    auto loaded_modules = *g_module_load_counter;
    auto unloaded_modules = *g_module_unload_counter;
    if (loaded_modules >= load) {
        *g_module_load_counter = loaded_modules - load;
        LOGD("reset g_module_load_counter to %zu", (size_t) *g_module_load_counter);
    }
    if (unloaded_modules >= unload) {
        *g_module_unload_counter = unloaded_modules - unload;
        LOGD("reset g_module_unload_counter to %zu", (size_t) *g_module_unload_counter);
    }
}
}  // namespace SoList
