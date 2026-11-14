#ifndef CONN_POOL_BOUND_SQL_BUILDER_H
#define CONN_POOL_BOUND_SQL_BUILDER_H




#include "xml/xml_types.h"
#include "xml/substitution_policy.h"
#include <memory>

namespace cp {

    class BoundSqlBuilder {
    public:
        BoundSqlBuilder(const SqlParams& params,
                        std::shared_ptr<ISubstitutionPolicy> policy = nullptr);

        BoundSql build(const std::shared_ptr<SqlNode>& root);

    private:
        void walk(const std::shared_ptr<SqlNode>& node,
                  BoundSql& out, SqlParams& scope);
        void emitText(const std::string& text, BoundSql& out, const SqlParams& scope);
        std::string expandTemplate(const std::string& tpl, const SqlParams& scope) const;

        static XMLParamValue evalValueExpr(const std::string& expr, const SqlParams& scope);
        static XMLParamValue evalSingleValue(const std::string& expr, const SqlParams& scope);
        static size_t       evalSize(const std::string& path, const SqlParams& scope);

        const SqlParams& params_;
        std::shared_ptr<ISubstitutionPolicy> policy_;
    };

} // namespace cp

#endif //CONN_POOL_BOUND_SQL_BUILDER_H