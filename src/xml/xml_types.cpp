#include "xml/xml_types.h"

namespace cp {

    const XMLParamValue* SqlParams::resolve(const std::string& path) const {
        size_t pos = path.find('.');
        if (pos == std::string::npos) return find(path);
        auto* v = find(path.substr(0, pos));
        if (!v) return nullptr;
        if (auto obj = std::get_if<std::shared_ptr<SqlParams>>(v)) {
            return (*obj ? (*obj)->resolve(path.substr(pos + 1)) : nullptr);
        }
        return nullptr;
    }

} // namespace cp