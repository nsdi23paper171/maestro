#include "memory_bank.h"

#define SPECIALIZE(value_type, prefix)                                         \
  template <>                                                                  \
  bool MemoryBank::contains<value_type>(Target target, uint64_t key) const {   \
    return prefix##_mb.contains(target, key);                                  \
  }                                                                            \
                                                                               \
  template <>                                                                  \
  value_type MemoryBank::read<value_type>(Target target, uint64_t key) const { \
    return prefix##_mb.read(target, key);                                      \
  }                                                                            \
                                                                               \
  template <>                                                                  \
  void MemoryBank::write<value_type>(Target target, uint64_t key,              \
                                     value_type value) {                       \
    prefix##_mb.write(target, key, value);                                     \
  }                                                                            \
  template <> bool MemoryBank::contains<value_type>(uint64_t key) const {      \
    return prefix##_mb.contains(key);                                          \
  }                                                                            \
                                                                               \
  template <> value_type MemoryBank::read<value_type>(uint64_t key) const {    \
    return prefix##_mb.read(key);                                              \
  }                                                                            \
                                                                               \
  template <>                                                                  \
  void MemoryBank::write<value_type>(uint64_t key, value_type value) {         \
    prefix##_mb.write(key, value);                                             \
  }

namespace synapse {

template <typename T>
bool MemoryBankTypeBase::contains(Target target, uint64_t key) const {
  return dynamic_cast<const MemoryBankType<T> &>(*this).contains(target, key);
}

template <typename T>
const T &MemoryBankTypeBase::read(Target target, uint64_t key) const {
  return dynamic_cast<const MemoryBankType<T> &>(*this).read(target, key);
}

template <typename T>
void MemoryBankTypeBase::write(Target target, uint64_t key, T value) {
  return dynamic_cast<MemoryBankType<T> &>(*this).write(target, key, value);
}

template <typename T> bool MemoryBankTypeBase::contains(uint64_t key) const {
  return dynamic_cast<const MemoryBankType<T> &>(*this).contains(key);
}

template <typename T> const T &MemoryBankTypeBase::read(uint64_t key) const {
  return dynamic_cast<const MemoryBankType<T> &>(*this).read(key);
}

template <typename T> void MemoryBankTypeBase::write(uint64_t key, T value) {
  return dynamic_cast<MemoryBankType<T> &>(*this).write(key, value);
}

SPECIALIZE(int, int)
SPECIALIZE(unsigned, unsigned)
SPECIALIZE(klee::ref<klee::Expr>, expr)

} // namespace synapse