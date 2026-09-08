#include <iostream>
#include <limits>
#include <ock/dynamic/binding/registered_record.hpp>

using namespace ock;
struct Args {
  std::uint64_t count{};
  double distance{};
  std::string unit;
  binding::Presence<std::string> label;
};
struct Spec {
  static contracts::TypeIdentity identity(){return {*foundation::Name::parse("test.binding"),*contracts::OperationVersion::parse("1.0.0",5),{}};}
  static auto fields() {
    auto count = binding::field<&Args::count>("count");
    count.minimum = 1;
    auto distance = binding::field<&Args::distance>("distance");
    distance.minimum = 0;
    distance.maximum = 100;
    distance.unit = "mm";
    auto unit = binding::field<&Args::unit>("unit");
    unit.enumeration = {"mm"};
    auto label = binding::field<&Args::label>("label");
    label.required = false;
    label.nullable = true;
    label.max_length = 3;
    return std::tuple(count, distance, unit, label);
  }
  static foundation::Result<void> validate(const Args &a) {
    if (a.count > 10 && a.distance == 0)
      return binding::reject();
    return {};
  }
};
using Contract = binding::Record<Args, Spec>;
namespace ock::contracts {template<> struct TypeContract<Args>:binding::TypeContract<Args,Spec>{};}
static_assert(contracts::AsyncInput<Args>);
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << "failed at " << __LINE__ << "\n";                           \
      return 1;                                                                \
    }                                                                          \
  } while (false)
int main() {
  auto registered = binding::RegisteredRecord<Args, Spec>::create();
  CHECK(registered);
  auto valid = data::Payload::parse(
      R"({"count":18446744073709551615,"distance":1,"unit":"mm"})");
  CHECK(valid);
  auto a = Contract::decode(valid->view());
  CHECK(a && a->count == UINT64_MAX &&
        a->label.state == binding::PresenceState::Missing);
  CHECK(Contract::validate(*a));
  CHECK(contracts::TypeContract<Args>::validate(*a));
  CHECK(registered->decode(valid->view()));
  auto roundtrip = Contract::encode(*a);
  CHECK(roundtrip && roundtrip->view().at("label").missing());
  for (auto input :
       {R"({"count":0,"distance":1,"unit":"mm"})",
        R"({"count":-1,"distance":1,"unit":"mm"})",
        R"({"count":1.0,"distance":1,"unit":"mm"})",
        R"({"count":1,"distance":1,"unit":"m"})",
        R"({"count":1,"distance":1,"unit":"mm","extra":1})",
        R"({"count":1,"unit":"mm"})",
        R"({"count":1,"distance":null,"unit":"mm"})",
        R"({"count":11,"distance":0,"unit":"mm"})",
        R"({"count":1,"distance":1,"unit":"mm","label":"four"})"}) {
    auto p = data::Payload::parse(input);
    CHECK(p && !Contract::decode(p->view()));
  }
  a->distance = std::numeric_limits<double>::quiet_NaN();
  CHECK(!Contract::validate(*a));
  a->distance = 101;
  CHECK(!Contract::validate(*a));
  a->distance = 1;
  a->unit = "m";
  CHECK(!Contract::validate(*a));
  auto null_label = data::Payload::parse(
      R"({"count":1,"distance":1,"unit":"mm","label":null})");
  auto b = Contract::decode(null_label->view());
  CHECK(b && b->label.state == binding::PresenceState::Null);
  auto encoded = Contract::encode(*b);
  CHECK(encoded && encoded->view().at("label").kind() == data::Kind::Null);
  auto unicode = data::Payload::parse(
      R"({"count":1,"distance":1,"unit":"mm","label":"中文好"})");
  CHECK(unicode && Contract::decode(unicode->view()));
  auto schema = Contract::schema();
  CHECK(schema);
  CHECK(schema->view().at("$schema").string() ==
        "https://json-schema.org/draft/2020-12/schema");
  CHECK(schema->view().at("properties").at("count").at("maximum").uint64() ==
        UINT64_MAX);
  CHECK(schema->view().at("additionalProperties").boolean() == false);
  std::cout << "Native/Dynamic field parity passed\n";
}
