#include "test_support.hpp"
#include "tests/contract/registration/fixtures.hpp"
#include "packages/runtime/registry/registry.hpp"
using namespace ock::runtime::registry;
void accepted(Registrar&r){auto o=OperationOptions{{},{},{name("module"),name("test")},{},false};r.read(read_handler,input(),o);r.state_edit(edit_handler,input(AtomicMode::StateEdit),o);r.external_effect(effect_handler,input(),o);r.lifecycle(transition_handler,input(),o);}
namespace ock::runtime::registry {template<>struct ConfigurationSnapshot<int>{static Result<int> freeze(const int& value){return value;}};}
auto accepted_configuration=ConfigurationBinding::make(3);

