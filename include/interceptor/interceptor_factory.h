#ifndef CONN_POOL_INTERCEPTOR_FACTORY_H
#define CONN_POOL_INTERCEPTOR_FACTORY_H

#include "interceptor/interceptor_chain.h"
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace cp {
    using InterceptorParams  = std::map<std::string, std::string>;
    using InterceptorCreator = std::function<std::shared_ptr<SqlInterceptor>(const InterceptorParams&)>;

    class InterceptorFactoryRegistry {
    public:
        static InterceptorFactoryRegistry& instance();
        void registerFactory(const std::string& name, InterceptorCreator c);
        std::shared_ptr<SqlInterceptor> create(const std::string& name,
                                               const InterceptorParams& p) const;
    private:
        InterceptorFactoryRegistry();
        std::map<std::string, InterceptorCreator> factories_;
    };

    struct InterceptorDeclInput {
        std::string name;
        int priority = 0;
        InterceptorParams params;
    };
    std::shared_ptr<InterceptorChain> buildInterceptorChain(
        const std::vector<InterceptorDeclInput>& decls);
} // namespace cp

#endif //CONN_POOL_INTERCEPTOR_FACTORY_H