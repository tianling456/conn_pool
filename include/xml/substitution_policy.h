#ifndef CONN_POOL_SUBSTITUTION_POLICY_H
#define CONN_POOL_SUBSTITUTION_POLICY_H



#include "xml/xml_types.h"
#include <set>
#include <stdexcept>

namespace cp {

    class ISubstitutionPolicy {
    public:
        virtual ~ISubstitutionPolicy() = default;
        virtual std::string resolve(const std::string& name, const XMLParamValue& v) = 0;
    };

    class DenyAllSubstitution : public ISubstitutionPolicy {
    public:
        std::string resolve(const std::string& name, const XMLParamValue&) override {
            throw std::runtime_error("${" + name + "} denied by policy");
        }
    };

    class SqlIdentifierSubstitution : public ISubstitutionPolicy {
    public:
        std::string resolve(const std::string& name, const XMLParamValue& v) override {
            std::string s = valueToString(v);
            for (char c : s) {
                if (std::isalnum(static_cast<unsigned char>(c))) continue;
                if (c == '_' || c == '.' || c == ' ' || c == ',' || c == '*') continue;
                throw std::runtime_error("${" + name + "} contains unsafe char");
            }
            return s;
        }
    };

    class WhitelistSubstitution : public ISubstitutionPolicy {
    public:
        void allow(std::string n) { names_.insert(std::move(n)); }
        std::string resolve(const std::string& name, const XMLParamValue& v) override {
            if (!names_.count(name))
                throw std::runtime_error("${" + name + "} not whitelisted");
            return valueToString(v);
        }
    private:
        std::set<std::string> names_;
    };

} // namespace cp

#endif //CONN_POOL_SUBSTITUTION_POLICY_H