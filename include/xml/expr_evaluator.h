#ifndef CONN_POOL_EXPR_EVALUATOR_H
#define CONN_POOL_EXPR_EVALUATOR_H



#include "xml/xml_types.h"
#include <optional>

namespace cp {

    class ExprEvaluator {
    public:
        explicit ExprEvaluator(const SqlParams& p) : params_(p) {}
        bool eval(const std::string& expr) const;

    private:
        bool evalAtom(const std::string& s) const;
        bool truthy(const std::string& path) const;
        bool compare(const std::string& lhs, const std::string& op,
                     const std::string& rhs) const;
        std::optional<bool>   evalBoolFunc(const std::string& expr) const;
        std::optional<double> evalNumeric(const std::string& expr) const;
        size_t                iterableSize(const std::string& path) const;
        bool                  iterableContains(const std::string& path,
                                               const std::string& valExpr) const;

        const SqlParams& params_;
    };

} // namespace cp

#endif //CONN_POOL_EXPR_EVALUATOR_H