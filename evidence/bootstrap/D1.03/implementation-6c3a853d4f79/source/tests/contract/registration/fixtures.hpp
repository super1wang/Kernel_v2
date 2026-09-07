#pragma once
#include "test_support.hpp"
inline EffectReport<int> effect_handler(const int&,EffectContext&) {return {id<EffectId>(),Application::Applied,BusinessStatus::Failed,{"receipt"},3,{}};}
inline TransitionReport<int> transition_handler(const int&,TransitionView&){return {id<TransitionId>(),id<foundation::ObjectId>(),name("Ready"),name("Failed"),1,BusinessStatus::Failed,3};}
