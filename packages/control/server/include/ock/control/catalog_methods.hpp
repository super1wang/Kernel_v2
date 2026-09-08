#pragma once
#include <ock/control/router.hpp>
#include <ock/dynamic/catalog/catalog.hpp>

namespace ock::control {
Result<std::vector<Method>>
    catalog_methods(std::shared_ptr<const catalog::Catalog>,
                    std::shared_ptr<const runtime::policy::SessionAuthority>,
                    foundation::ObjectId);
}
