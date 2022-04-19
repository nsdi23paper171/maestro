#pragma once

#include "../modules/module.h"

#include <typeindex>
#include <unordered_map>

namespace synapse {

class MemoryBankTypeBase {
public:
  template <typename T> inline bool contains(Target target, uint64_t key) const;
  template <typename T> inline const T &read(Target target, uint64_t key) const;
  template <typename T> inline void write(Target target, uint64_t key, T value);

  template <typename T> inline bool contains(uint64_t key) const;
  template <typename T> inline const T &read(uint64_t key) const;
  template <typename T> inline void write(uint64_t key, T value);
};

template <typename T> class MemoryBankType {
private:
  std::unordered_map<Target, std::map<int, T>> banks;
  std::map<int, T> common_bank;

public:
  bool contains(uint64_t key) const {
    auto value_it = common_bank.find(key);

    if (value_it == common_bank.end()) {
      return false;
    }

    return true;
  }

  T read(uint64_t key) const {
    assert(contains(key));
    return common_bank.at(key);
  }

  void write(uint64_t key, T value) { common_bank[key] = value; }

  bool contains(Target target, uint64_t key) const {
    auto bank_it = banks.find(target);

    if (bank_it == banks.end()) {
      return false;
    }

    auto value_it = bank_it->second.find(key);

    if (value_it == bank_it->second.end()) {
      return false;
    }

    return true;
  }

  T read(Target target, uint64_t key) const {
    assert(contains(target, key));
    return banks.at(target).at(key);
  }

  void write(Target target, uint64_t key, T value) {
    banks[target][key] = value;
  }
};

class MemoryBank {
private:
  MemoryBankType<int> int_mb;
  MemoryBankType<unsigned> unsigned_mb;
  MemoryBankType<klee::ref<klee::Expr>> expr_mb;

public:
  template <typename T> bool contains(Target target, uint64_t key) const {
    assert(false && "MemoryBank: I dont know this type");
  }
  template <typename T> T read(Target target, uint64_t key) const {
    assert(false && "MemoryBank: I dont know this type");
  }
  template <typename T> void write(Target target, uint64_t key, T value) {
    assert(false && "MemoryBank: I dont know this type");
  }

  template <typename T> bool contains(uint64_t key) const {
    assert(false && "MemoryBank: I dont know this type");
  }
  template <typename T> T read(uint64_t key) const {
    assert(false && "MemoryBank: I dont know this type");
  }
  template <typename T> void write(uint64_t key, T value) {
    assert(false && "MemoryBank: I dont know this type");
  }
};

} // namespace synapse
