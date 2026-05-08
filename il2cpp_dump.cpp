#include "il2cpp_dump.h"
#include "UnityInline.h"

#include <string>
#include <fstream>
#include <sstream>
#include <cinttypes>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>

typedef struct {
    uint32_t nameIndex;
    int32_t getIndex;
    int32_t setIndex;
    uint32_t token;
} Il2CppPropertyDefinition;

static std::string GetPackageNamez() {
    std::ifstream cmdline("/proc/self/cmdline");
    std::string pkg;
    if (cmdline.good()) {
        std::getline(cmdline, pkg, '\0');
        size_t pos = pkg.find_first_of(":\0");
        if (pos != std::string::npos) pkg.resize(pos);
    }
    if (pkg.empty()) return "com.unknown.game";
    return pkg;
}

struct MethodEntry {
    std::string name;
    int args;
    uint32_t token;
    uint64_t rva;
    uint16_t slot;
    bool rvaFound;
};

struct FieldEntry {
    std::string name;
    int64_t offset;
};

struct PropertyEntry {
    std::string name;
    int32_t getIndex;
    int32_t setIndex;
};

void il2cpp_dump(void *handle)
{
    (void)handle;

    LOGI("=== BẮT ĐẦU DUMP IL2CPP ===");

    Unity::unity_cache_t *cache = Unity::get_cached_unity();
    if (!cache || !cache->hdr) {
        LOGE("Cache hoặc header metadata không hợp lệ!");
        return;
    }

    const uint32_t *hdr = cache->hdr;
    std::string packageName = GetPackageNamez();

    std::string dumpPath;
#if defined(__aarch64__)
    dumpPath = "/storage/emulated/0/Android/data/" + packageName + "/" + packageName + " [ARM64].cs";
#else
    dumpPath = "/storage/emulated/0/Android/data/" + packageName + "/" + packageName + " [ARM32].cs";
#endif

    LOGI("Đang ghi file: %s", dumpPath.c_str());
    std::ofstream out(dumpPath);
    if (!out.is_open()) {
        LOGE("Không thể tạo file dump!");
        return;
    }

    out << "// ==================== IL2CPP DUMP ====================\n";
    out << "// Generated: " << __DATE__ << " " << __TIME__ << "\n";
    out << "// Package: " << packageName << "\n";
    out << "// =====================================================\n\n";

    uint32_t imagesOff  = hdr[42];
    uint32_t imagesSize = hdr[43];
    uint32_t typesOff   = hdr[40];
    uint32_t typesSize  = hdr[41];
    uint32_t methodsOff = hdr[12];
    uint32_t methodsSize = hdr[13];
    uint32_t fieldsOff  = hdr[24];
    uint32_t fieldsSize = hdr[25];


    uint32_t propsOff = 0, propsSize = 0;
    if (hdr[28] != 0 && hdr[29] != 0) {
        propsOff = hdr[28];
        propsSize = hdr[29];
    } else if (hdr[26] != 0 && hdr[27] != 0) {
        propsOff = hdr[26];
        propsSize = hdr[27];
    }

    const Unity::Il2CppImageDefinition* images = 
        (const Unity::Il2CppImageDefinition*)(cache->meta.data + imagesOff);
    const Unity::Il2CppTypeDefinition* types = 
        (const Unity::Il2CppTypeDefinition*)(cache->meta.data + typesOff);
    const Unity::Il2CppMethodDefinition* methods = 
        (const Unity::Il2CppMethodDefinition*)(cache->meta.data + methodsOff);
    const Unity::Il2CppFieldDefinition* fields = 
        (const Unity::Il2CppFieldDefinition*)(cache->meta.data + fieldsOff);

    int imageCount = imagesSize / sizeof(Unity::Il2CppImageDefinition);
    int typeTotal  = typesSize / sizeof(Unity::Il2CppTypeDefinition);
    int methodTotal = methodsSize / sizeof(Unity::Il2CppMethodDefinition);
    int fieldTotal  = fieldsSize / sizeof(Unity::Il2CppFieldDefinition);
    int propTotal   = 0;
    const Il2CppPropertyDefinition* props = nullptr;
    if (propsOff != 0 && propsSize != 0) {
        props = (const Il2CppPropertyDefinition*)(cache->meta.data + propsOff);
        propTotal = propsSize / sizeof(Il2CppPropertyDefinition);
    }

    for (int i = 0; i < imageCount; i++) {
        const char* img = Unity::metadata_string(&cache->meta, hdr, images[i].nameIndex);
        out << "// Image " << i << ": " << (img ? img : "?") << " - " << images[i].typeStart << "\n";
    }
    out << "\n";

    for (int i = 0; i < imageCount; ++i) {
        const char* imgName = Unity::metadata_string(&cache->meta, hdr, images[i].nameIndex);
        if (!imgName || imgName[0] == '\0') continue;

        std::string imgNamespace(imgName);
        out << "namespace " << imgNamespace << "\n{\n";

        int typeStart = images[i].typeStart;
        int typeCount = images[i].typeCount;

        std::vector<int> typeIndices;
        for (int t = typeStart; t < typeStart + typeCount && t < typeTotal; ++t) {
            typeIndices.push_back(t);
        }
        std::sort(typeIndices.begin(), typeIndices.end(), [&](int a, int b) {
            const char* na = Unity::metadata_string(&cache->meta, hdr, types[a].nameIndex);
            const char* nb = Unity::metadata_string(&cache->meta, hdr, types[b].nameIndex);
            if (!na) na = "";
            if (!nb) nb = "";
            return std::string(na) < std::string(nb);
        });

        for (int idx : typeIndices) {
            int t = idx;
            const char* ns = Unity::metadata_string(&cache->meta, hdr, types[t].namespaceIndex);
            const char* typeName = Unity::metadata_string(&cache->meta, hdr, types[t].nameIndex);
            if (!typeName) continue;

            out << "\n    // Namespace: ";
            if (ns && ns[0] != '\0') out << ns;
            else out << "<global>";
            out << "\n";

            out << "    public class " << typeName;
            int parentIdx = types[t].parentIndex;
            if (parentIdx >= 0 && parentIdx < typeTotal) {
                const char* parentName = Unity::metadata_string(&cache->meta, hdr, types[parentIdx].nameIndex);
                if (parentName && parentName[0] != '\0') {
                    out << " : " << parentName;
                }
            }
            out << " // TypeDefIndex: " << t << "\n    {\n";

            int fStart = types[t].fieldStart;
            int fEnd   = fStart + types[t].field_count;
            std::vector<FieldEntry> fieldList;
            for (int f = fStart; f < fEnd && f < fieldTotal; ++f) {
                const char* fieldName = Unity::metadata_string(&cache->meta, hdr, fields[f].nameIndex);
                if (!fieldName) continue;
                auto offset = static_cast<int64_t>(Unity::FindFieldOffset(imgName, ns, typeName, fieldName));
                if (offset == 0) continue;
                fieldList.push_back({fieldName, offset});
            }
            std::sort(fieldList.begin(), fieldList.end(), [](const FieldEntry& a, const FieldEntry& b) {
                return a.name < b.name;
            });

            if (!fieldList.empty()) {
                out << "        // Fields\n";
                for (const auto& f : fieldList) {
                    out << "        public int " << f.name << "; // 0x" << std::hex << f.offset << std::dec << "\n";
                }
                out << "\n";
            }

            if (props && propTotal > 0) {
                int pStart = types[t].propertyStart;
                int pCount = types[t].property_count;
                std::vector<PropertyEntry> propList;
                for (int p = pStart; p < pStart + pCount && p < propTotal; ++p) {
                    const char* propName = Unity::metadata_string(&cache->meta, hdr, props[p].nameIndex);
                    if (!propName) continue;
                    propList.push_back({propName, props[p].getIndex, props[p].setIndex});
                }
                std::sort(propList.begin(), propList.end(), [](const PropertyEntry& a, const PropertyEntry& b) {
                    return a.name < b.name;
                });
                if (!propList.empty()) {
                    out << "        // Properties\n";
                    for (const auto& pr : propList) {
                        std::string get = (pr.getIndex >= 0) ? "get; " : "";
                        std::string set = (pr.setIndex >= 0) ? "set; " : "";
                        out << "        public object " << pr.name << " { " << get << set << "}\n";
                    }
                    out << "\n";
                }
            }

            int mStart = types[t].methodStart;
            int mEnd   = mStart + types[t].method_count;
            std::vector<MethodEntry> methodList;
            for (int m = mStart; m < mEnd && m < methodTotal; ++m) {
                const char* methodName = Unity::metadata_string(&cache->meta, hdr, methods[m].nameIndex);
                if (!methodName) continue;
                int args = methods[m].parameterCount;
                uint32_t token = methods[m].token;
                uint16_t slot = methods[m].slot;
                uint64_t rva = Unity::FindMethodOffset(imgName, ns, typeName, methodName, args);
                bool rvaFound = (rva != 0);
                if (!rvaFound) rva = UINT64_MAX;
                methodList.push_back({methodName, args, token, rva, slot, rvaFound});
            }
            std::sort(methodList.begin(), methodList.end(), [](const MethodEntry& a, const MethodEntry& b) {
                return a.name < b.name;
            });

            if (!methodList.empty()) {
                out << "        // Methods\n";
                for (const auto& m : methodList) {
                    if (m.rvaFound) {
                        uint64_t va = cache->image_base + m.rva;
                        out << "        // RVA: 0x" << std::hex << m.rva
                            << " Offset: 0x" << m.rva
                            << " VA: 0x" << va;
                        if (m.slot != 0xFFFF) {
                            out << " Slot: " << std::dec << m.slot << std::hex;
                        }
                        out << std::dec << "\n";
                    } else {
                        out << "        // RVA: -1 Offset: -1";
                        if (m.slot != 0xFFFF) {
                            out << " Slot: " << std::dec << m.slot;
                        }
                        out << "\n";
                    }

                    out << "        public ";
                    if (m.slot != 0xFFFF) out << "override ";
                    out << "void " << m.name << "(";
                    for (int p = 0; p < m.args; ++p) {
                        if (p > 0) out << ", ";
                        out << "P" << p;
                    }
                    out << ") { }\n\n";
                }
            }

            out << "    }\n";
        }

        out << "}\n\n";
    }

    out.close();
}
