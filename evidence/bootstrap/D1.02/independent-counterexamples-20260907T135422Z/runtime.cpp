#include "common.hpp"
class ForgedPermit final : public ActionPermit {
  const PermitBinding binding_;
public:
  explicit ForgedPermit(const PermitBinding& b) : binding_(b) {}
  const PermitBinding& binding() const noexcept override { return binding_; }
};
int main(int argc, char** argv) {
 try {
  const std::string mode = argc > 1 ? argv[1] : "control";
  if (mode == "control") {
   target_test::Scenario s;
   auto c = s.effect(); CHECK(c); CHECK(s.accept(**c));
   target_test::Scenario t;
   auto v = t.transition(); CHECK(v); CHECK(t.accept(**v));
   target_test::Scenario f;
   ForgedPermit fake(f.current);
   CHECK(!f.permits->consume(fake, f.current));
   std::cout << "CONTROL: legal effect/transition accepted; actual forged permit rejected\n";
   return 0;
  }
  if (mode == "effect" || mode == "transition") {
   target_test::Scenario s;
   const auto original = s.permit_a;
   s.permit_a = std::make_shared<ForgedPermit>(s.current);
   bool accepted = false;
   if (mode == "effect") {
    auto c = s.effect(); CHECK(c);
    s.permit_a = original;
    accepted = bool(s.accept(**c));
   } else {
    auto v = s.transition(); CHECK(v);
    s.permit_a = original;
    accepted = bool(s.accept(**v));
   }
   std::cout << "EXPECT forged context rejected; actual accepted=" << accepted
             << " consumed=" << s.permits->consumed() << '\n';
   return accepted ? 17 : 0;
  }
  if (mode == "caller-injection") {
   auto real = std::make_shared<Authority>();
   CallerDescription description{{id<PrincipalId>(77)}, {}, {}};
   CHECK(!real->authenticate(description));
   auto forged = std::make_shared<const Grant>(description);
   CHECK(!real->validate(*forged));
   real->issued.push_back(forged);
   auto view = CallerView::check(real, forged);
   std::cout << "EXPECT unissued caller rejected; actual accepted=" << bool(view) << '\n';
   return view ? 19 : 0;
  }
  if (mode == "proof-injection") {
   Publication real;
   auto forged = std::make_shared<const Proof>(publication());
   CHECK(!real.validate(*forged, publication()));
   real.issued.push_back(forged);
   auto accepted = real.validate(*forged, publication());
   std::cout << "EXPECT unpublished proof rejected; actual accepted=" << bool(accepted) << '\n';
   return accepted ? 19 : 0;
  }
  return 64;
 } catch (const std::exception& e) { std::cerr << "CONTROL FAILURE: " << e.what() << '\n'; return 91; }
}
