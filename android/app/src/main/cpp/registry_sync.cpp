#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

#define LOG_TAG "NFSU2_RegistrySync"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Red-black tree node layout matching libnfsu2_android.so (LLVM libc++ 64-bit)
struct NfsRegNode {
    NfsRegNode* left;       // 0x00
    NfsRegNode* right;      // 0x08
    NfsRegNode* parent;     // 0x10
    uintptr_t is_black;     // 0x18
    // libc++ std::string key (24 bytes) at 0x20
    uint8_t key_raw[24];    // 0x20 .. 0x37
    uint32_t type;          // 0x38 (4 = REG_DWORD, 1 = REG_SZ)
    uint32_t pad;           // 0x3c
    uint8_t* vec_begin;     // 0x40 (std::vector<uint8_t>)
    uint8_t* vec_end;       // 0x48
    uint8_t* vec_cap;       // 0x50
};

static uintptr_t g_libBase = 0;
static std::string g_iniPath = "/sdcard/nfsu2/registry.ini";

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

static uintptr_t find_library_base(const char* libname) {
    FILE* fp = fopen("/proc/self/maps", "r");
    if (!fp) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, libname)) {
            uintptr_t start = 0;
            if (sscanf(line, "%lx-", &start) == 1) {
                base = start;
                break;
            }
        }
    }
    fclose(fp);
    return base;
}

static std::string get_node_key(const NfsRegNode* node) {
    if (!node) return "";
    uint8_t flag = node->key_raw[0];
    if ((flag & 1) == 0) {
        // Short string (SSO): length is flag >> 1, chars at key_raw + 1
        size_t len = flag >> 1;
        if (len > 22) len = 22;
        return std::string((const char*)&node->key_raw[1], len);
    } else {
        // Long string: size at offset 8, pointer at offset 16
        size_t len = *(const size_t*)&node->key_raw[8];
        const char* ptr = *(const char* const*)&node->key_raw[16];
        if (ptr && len < 1024) {
            return std::string(ptr, len);
        }
    }
    return "";
}

static void set_node_key(NfsRegNode* node, const std::string& key) {
    if (key.length() <= 22) {
        node->key_raw[0] = (uint8_t)(key.length() << 1);
        memcpy(&node->key_raw[1], key.data(), key.length());
        node->key_raw[1 + key.length()] = '\0';
    } else {
        node->key_raw[0] = 1;
        *(size_t*)&node->key_raw[8] = key.length();
        char* buf = (char*)malloc(key.length() + 1);
        memcpy(buf, key.data(), key.length() + 1);
        *(char**)&node->key_raw[16] = buf;
    }
}

static void update_node_value(NfsRegNode* node, uint32_t type, const std::vector<uint8_t>& bytes) {
    node->type = type;
    size_t curCap = node->vec_cap ? (node->vec_cap - node->vec_begin) : 0;
    if (curCap < bytes.size() || !node->vec_begin) {
        if (node->vec_begin) free(node->vec_begin);
        size_t allocSize = bytes.size() > 0 ? bytes.size() : 1;
        node->vec_begin = (uint8_t*)malloc(allocSize);
        node->vec_cap = node->vec_begin + allocSize;
    }
    if (!bytes.empty()) {
        memcpy(node->vec_begin, bytes.data(), bytes.size());
    }
    node->vec_end = node->vec_begin + bytes.size();
}

static void collect_nodes(NfsRegNode* root, std::vector<NfsRegNode*>& out) {
    if (!root) return;
    collect_nodes(root->left, out);
    out.push_back(root);
    collect_nodes(root->right, out);
}

static void insert_or_update(uintptr_t libBase, const std::string& key, uint32_t type, const std::vector<uint8_t>& bytes) {
    volatile void** pRoot = (volatile void**)(libBase + 0x97ca8d0);
    volatile uint64_t* pSize = (volatile uint64_t*)(libBase + 0x97ca8d8);
    volatile void** pBegin = (volatile void**)(libBase + 0x97ca8c8);

    NfsRegNode* root = (NfsRegNode*)*pRoot;
    NfsRegNode* cur = root;
    NfsRegNode* parent = nullptr;
    bool is_left = false;

    while (cur) {
        parent = cur;
        std::string cur_k = get_node_key(cur);
        if (key < cur_k) {
            cur = cur->left;
            is_left = true;
        } else if (key > cur_k) {
            cur = cur->right;
            is_left = false;
        } else {
            // Found existing node; update value
            update_node_value(cur, type, bytes);
            return;
        }
    }

    // Node does not exist; allocate new node (0x58 = 88 bytes)
    NfsRegNode* newNode = (NfsRegNode*)calloc(1, 0x58);
    newNode->parent = parent;
    set_node_key(newNode, key);
    update_node_value(newNode, type, bytes);

    if (!parent) {
        *pRoot = newNode;
        newNode->is_black = 1;
        *pBegin = newNode;
        *pSize = 1;
        return;
    }

    if (is_left) {
        parent->left = newNode;
        if (parent == *pBegin) {
            *pBegin = newNode;
        }
    } else {
        parent->right = newNode;
    }

    // Call __tree_balance_after_insert(root, newNode)
    typedef void (*balance_fn)(void* root, void* newNode);
    balance_fn balance = (balance_fn)(libBase + 0x447b68);
    balance((void*)*pRoot, newNode);

    (*pSize)++;
}

static void save_registry_from_tree(uintptr_t libBase, const std::string& path) {
    volatile void** pRoot = (volatile void**)(libBase + 0x97ca8d0);
    if (!pRoot || !*pRoot) return;

    std::vector<NfsRegNode*> nodes;
    collect_nodes((NfsRegNode*)*pRoot, nodes);
    if (nodes.empty()) return;

    std::string tmpPath = path + ".tmp";
    FILE* fp = fopen(tmpPath.c_str(), "w");
    if (!fp) {
        LOGW("Failed to open %s for saving registry", tmpPath.c_str());
        return;
    }

    fprintf(fp, "# NFSU2 Android Evolved - Persistent Game Registry\n");
    fprintf(fp, "[Registry]\n");

    for (NfsRegNode* node : nodes) {
        std::string k = get_node_key(node);
        if (k.empty()) continue;
        if (node->type == 4 && node->vec_begin && (node->vec_end - node->vec_begin) >= 4) {
            uint32_t val = *(uint32_t*)node->vec_begin;
            fprintf(fp, "%s=%u\n", k.c_str(), val);
        } else if (node->type == 1 && node->vec_begin) {
            size_t len = node->vec_end - node->vec_begin;
            if (len > 0 && node->vec_begin[len - 1] == '\0') len--;
            std::string s((const char*)node->vec_begin, len);
            fprintf(fp, "%s=\"%s\"\n", k.c_str(), s.c_str());
        }
    }

    fclose(fp);
    rename(tmpPath.c_str(), path.c_str());
    LOGI("Saved %zu registry entries to %s", nodes.size(), path.c_str());
}

static void apply_registry_file_to_tree(uintptr_t libBase, const std::string& path) {
    FILE* fp = fopen(path.c_str(), "r");
    if (!fp) {
        LOGI("No registry file at %s, applying optimized defaults", path.c_str());
        auto set_dw = [&](const char* k, uint32_t v) {
            std::vector<uint8_t> b(4);
            memcpy(b.data(), &v, 4);
            insert_or_update(libBase, to_lower(k), 4, b);
        };
        auto set_sz = [&](const char* k, const char* v) {
            std::vector<uint8_t> b(v, v + strlen(v) + 1);
            insert_or_update(libBase, to_lower(k), 1, b);
        };

        set_sz("install dir", "C:\\nfsu2\\");
        set_sz("language", "English UK");
        set_dw("g_performancelevel", 6);
        set_dw("g_carenvironmentmapenable", 1);
        set_dw("g_carheadlightenable", 1);
        set_dw("g_cardamageenable", 1);
        set_dw("g_carspecialexhaustenable", 1);
        set_dw("g_crowdenable", 1);
        set_dw("g_fogenable", 1);
        set_dw("g_fsaalevel", 2);
        set_dw("g_horizonenable", 1);
        set_dw("g_motionblurenable", 1);
        set_dw("g_lighttrailsenable", 1);
        set_dw("g_lightglowenable", 1);
        set_dw("g_overbrightenable", 1);
        set_dw("g_particlesystemenable", 1);
        set_dw("g_depthoffieldenable", 1);
        set_dw("g_roadreflectionenable", 2);
        set_dw("g_shadowdetail", 2);
        set_dw("g_visualtreatment", 1);
        set_dw("g_worldlodlevel", 3);
        set_dw("g_worldroadlodlevel", 3);
        set_dw("g_rainenable", 1);
        set_dw("size", 1280);
        set_dw("sizex", 1280);
        set_dw("sizey", 720);
        set_dw("racingresolution", 0);

        save_registry_from_tree(libBase, path);
        return;
    }

    char line[512];
    int loaded = 0;
    while (fgets(line, sizeof(line), fp)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == '[' || *p == '\n' || *p == '\r' || *p == '\0') continue;
        char* eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        std::string key = to_lower(p);
        char* val = eq + 1;
        size_t len = strlen(val);
        while (len > 0 && (val[len - 1] == '\r' || val[len - 1] == '\n' || val[len - 1] == ' ')) {
            val[--len] = '\0';
        }
        if (*val == '"') {
            val++;
            char* endq = strrchr(val, '"');
            if (endq) *endq = '\0';
            std::vector<uint8_t> bytes(val, val + strlen(val) + 1);
            insert_or_update(libBase, key, 1, bytes);
            loaded++;
        } else {
            char* endp = nullptr;
            unsigned long num = strtoul(val, &endp, 0);
            uint32_t dw = (uint32_t)num;
            std::vector<uint8_t> bytes(4);
            memcpy(bytes.data(), &dw, 4);
            insert_or_update(libBase, key, 4, bytes);
            loaded++;
        }
    }
    fclose(fp);
    LOGI("Loaded and synced %d registry entries from %s", loaded, path.c_str());
}

static void* registry_sync_thread(void*) {
    LOGI("Watchdog started, waiting for libnfsu2_android.so...");
    while (!g_libBase) {
        g_libBase = find_library_base("libnfsu2_android.so");
        if (!g_libBase) usleep(10000); // 10ms
    }
    LOGI("Found libnfsu2_android.so at base 0x%lx", (unsigned long)g_libBase);

    volatile void** pRoot = (volatile void**)(g_libBase + 0x97ca8d0);
    volatile uint64_t* pSize = (volatile uint64_t*)(g_libBase + 0x97ca8d8);
    pthread_mutex_t* mtx = (pthread_mutex_t*)(g_libBase + 0x97caa10);

    // Wait until libnfsu2_android.so initializes its registry tree
    while (*pRoot == nullptr || *pSize == 0) {
        usleep(10000); // 10ms
    }

    LOGI("Registry tree initialized by game (size=%lu, root=%p)", (unsigned long)*pSize, *pRoot);

    // Initial load: inject saved settings into game's registry
    pthread_mutex_lock(mtx);
    apply_registry_file_to_tree(g_libBase, g_iniPath);
    pthread_mutex_unlock(mtx);

    auto compute_hash = [pRoot]() -> uint32_t {
        uint32_t h = 0x811c9dc5u;
        if (!*pRoot) return h;
        std::vector<NfsRegNode*> nodes;
        collect_nodes((NfsRegNode*)*pRoot, nodes);
        for (NfsRegNode* node : nodes) {
            std::string k = get_node_key(node);
            for (char c : k) h = (h ^ uint8_t(c)) * 0x01000193u;
            h = (h ^ uint8_t(node->type)) * 0x01000193u;
            if (node->vec_begin && node->vec_end > node->vec_begin) {
                for (uint8_t* p = node->vec_begin; p < node->vec_end; ++p) {
                    h = (h ^ *p) * 0x01000193u;
                }
            }
        }
        return h;
    };

    pthread_mutex_lock(mtx);
    uint32_t lastHash = compute_hash();
    pthread_mutex_unlock(mtx);

    while (true) {
        sleep(1);
        pthread_mutex_lock(mtx);
        uint32_t curHash = compute_hash();
        if (curHash != lastHash) {
            LOGI("Registry change detected in game, saving to %s...", g_iniPath.c_str());
            save_registry_from_tree(g_libBase, g_iniPath);
            lastHash = curHash;
        }
        pthread_mutex_unlock(mtx);
    }

    return nullptr;
}

extern "C" JNIEXPORT void JNICALL
Java_com_nfsu2_androidevolved_GameActivity_nativeSyncSave(JNIEnv*, jclass) {
    if (g_libBase) {
        pthread_mutex_t* mtx = (pthread_mutex_t*)(g_libBase + 0x97caa10);
        pthread_mutex_lock(mtx);
        save_registry_from_tree(g_libBase, g_iniPath);
        pthread_mutex_unlock(mtx);
    }
}

__attribute__((constructor))
static void registry_sync_init() {
    LOGI("Initializing registry_sync library");
    pthread_t thread;
    pthread_create(&thread, nullptr, registry_sync_thread, nullptr);
    pthread_detach(thread);
}
