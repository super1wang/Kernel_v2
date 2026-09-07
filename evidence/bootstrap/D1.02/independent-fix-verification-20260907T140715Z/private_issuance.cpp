#include "common.hpp"
template<class T> concept PublicIssuance = requires(T& x) { x.issued; };
static_assert(!PublicIssuance<Authority>, "REVIEW_CALLER_ISSUANCE_MUST_BE_PRIVATE");
static_assert(!PublicIssuance<Publication>, "REVIEW_PUBLICATION_ISSUANCE_MUST_BE_PRIVATE");
int main() { return 0; }
